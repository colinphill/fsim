<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v1 resume handoff

This is the short entry point for resuming v1 implementation. The
[implementation plan](implementation-plan.md) remains the chronological record,
and the [feature matrix](feature-matrix.md) remains the release authority.

Maintain a Batch 101-style numbered list of exactly ten implementation tasks
for the current feature batch in both this handoff and the implementation plan.
Keep the current batch explicitly **in progress** and update its task evidence
as work lands. Before moving on, retain that batch's list, mark it
**complete**, and create the next batch's ten-task list as the new current
**in progress** record.

Treat those ten tasks as one implementation and release unit. Keep Tasks 1
through 9 as a recoverable dirty working tree with concise status updates and
the smallest focused tests after each coherent change; do not sanitize,
commit, push, or run full Debug/Release regressions per task. Task 10 owns one
accumulated sanitizer/source/catalog/full-regression/documentation/commit/push
gate. Create an earlier commit only for an explicitly requested shutdown or a
risky structural transition that needs a durable boundary.

## Snapshot

- Recorded: 2026-08-03.
- Branch: `codex/resumable-jit`.
- Implementation baseline: Batch 130 is complete and mandatory
  non-documentation GitHub Actions run `30839583995` passed all 12 jobs.
  Corrective Batch 131 is complete at pushed commit `5e5da76`. The current
  working tree adds the post-release three-language hierarchy example and its
  automated application regression; its focused Debug and release-inventory
  gates pass.
  Verify live Git state before resuming; do not discard a newer intentional
  checkpoint.
- The source-size gate has a 2,500-line hard limit. If a file exceeds it, the
  required repair target is below 2,000 lines; exceptions and allowlists are
  not permitted.
- The post-Batch-110 Debug-footprint repair partitions the 116-alternative
  SimIR `Operation` into eight semantic storage groups while retaining flat
  construction, query, and visitation helpers. Five large visitors now use a
  single typed dispatch body rather than a 116-lambda overload set, and the
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
- The diagnostic catalog covers all 1,624 production codes.
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

## Completed feature batch 113

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

Batch status is **complete**. All ten tasks and their gates are closed, and the
current in-progress ten-task record has moved to Batch 114 below.

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
O0/O2/cache, catalog, and 343-source line-budget evidence passes in the LLVM
Debug tree. All ten Batch 113 tasks are now complete.

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

Task 4 is now focused-complete. Direct
`configuration [library.]name` instances have distinct retained HIR, optional
generic/port maps, previously-analyzed configuration lookup, root-architecture
selection, recursive nested-rule activation, source provenance, and canonical
configuration cache identity. Missing and ambiguous declarations retain
`FSIM-ELAB-VHCONFIG-013/014`; direct use before analysis reports
`FSIM-FE-VHORDER-008`, including inside generate bodies. The configuration
application proves interpreter and LLVM O0/O2 cold/warm equivalence and
configuration-edit selective invalidation for the direct form. The latest
focused frontend, elaboration, analysis-order, configuration-application,
1,436-code catalog, and 343-source line-budget gates pass in the LLVM Debug
tree.

Task 2 is now focused-complete. The parser and elaborator jointly cover legal
positional, named, positional-then-named, explicit `open`, conversions, and
dependent static actuals, plus deterministic rejection of unknown, excessive,
duplicate, named-then-positional, missing/defaultless-open, and non-static
associations. The elaboration matrix directly checks
`FSIM-ELAB-GENERIC-001` through `004`; the existing component application
retains interpreter and LLVM O0/O2 execution evidence for the supported forms.
Task 2 is focused-complete.

Task 6 is now focused-complete. The parser retains
`type_mark'(expression)` qualification as explicit HIR. Input-port elaboration
checks the mark against the formal, materializes qualified static
literals/aggregates, and drives qualified dynamic slices and converted
concatenations through the existing per-instance boundary path. A mismatched
mark reports `FSIM-ELAB-VHPORT-001`, and non-input expressions retain
`FSIM-ELAB-VHPORT-002`. The component interpreter and LLVM O0/O2 cold/warm
differential covers both qualified and converted runtime expressions.

Task 7 is now focused-complete. VHDL port maps accept positional-then-named
associations and diagnose duplicate names or named-then-positional ordering as
`FSIM-VHDL-SEM-078/079`; component normalization also rejects malformed
retained order. Omitted/open inputs use retained defaults, required inputs keep
their component/direct diagnostics, and omitted or explicit-open output/buffer
ports execute through child-local signals without a parent driver. The
component interpreter and LLVM O0/O2 cold/warm differential observes values 5
and 7 from open output and buffer formals. The current catalog covers 1,438
codes and the source gate accepts all 343 authored sources. Task 7 is
focused-complete.

Task 8 is now focused-complete. Generic-dependent port constraints specialize
component views and direct instances before connection, with child hierarchy
names aliasing the exact parent signals. Wrong widths retain
`FSIM-ELAB-BIND-020`; equal-width VHDL packed arrays with mismatched bounds or
direction now report `FSIM-ELAB-BIND-031` rather than silently aliasing
different index maps. The component application executes distinct 4-bit and
8-bit component/direct specializations across interpreter and LLVM O0/O2
cold/warm paths. The catalog now covers 1,439 codes and all 343 authored
sources pass the line gate.

Task 5 is now focused-complete. `vhdl-component-binding-v6` retains effective
value/type/function/procedure/package bindings, component profiles, defaults,
selected targets, binding/configuration identity, and transitive source
provenance while treating a direct parent signal actual as a per-instance alias
rather than specialization content. Two width-4 component instances connected
to differently named parent signals now have identical specialization keys; a
width-8 direct instance remains distinct. Warm-cache reuse and the existing
selective default, callable, profile, and configuration-edit invalidation
checks remain exact.

Task 9 is now focused-complete. The component application executes the two
width-4 component instances and one width-8 direct instance identically in the
interpreter and LLVM O0/O2 cold/warm paths, checks exact hierarchy aliases and
source debugger points, and compares deterministic VCD containing all three
specialized outputs. Existing source-edit differentials retain selective key
invalidation and warm native-cache hits.

Task 10 is complete. Exact LLVM 22.1.8 warnings-as-errors Debug passed all 66
tests in 170.07 seconds, including the component application in 0.87 seconds
and `fsim.application.scoped_locals` in 0.83 seconds. Release passed all 66 in
156.91 seconds, including the component application in 0.91 seconds and scoped
locals in 0.84 seconds. The LLVM-disabled ASan/UBSan focused six-test gate
passed in 3.13 seconds with LeakSanitizer disabled only because the managed
ptrace environment cannot start it. The diagnostic catalog retains 1,439
production codes and all 343 authored sources pass the 2,000-line gate;
`elaboration_vhdl_components.cpp` is exactly 2,000 lines. Component binding
identity is now version 6; native-object schema 69, the public runtime ABI, and
container semantic revision 28 are unchanged. All ten Batch 113 tasks are
complete and checkpoint `4af01cd` contains the final implementation slice.

## Completed feature batch 114

1. **Complete.** Retain guarded block syntax, guard expressions, optional `is`, opening/end
   labels, and exact source regions with checked unsupported-form diagnostics.
2. **Complete.** Elaborate implicit Boolean `GUARD`, guard sensitivity and activation, nested
   scope identity, and invalid/non-Boolean guard diagnostics.
3. **Complete.** Complete block generic/port clauses and maps, defaults, `open`, profile
   legality, hierarchy aliases, and specialization/cache provenance.
4. **Complete.** Complete declarative parts for `if`, `for`, and `case` generate alternatives
   before `begin`, retaining alternative and iteration scope.
5. **Complete.** Add generated type/subtype declarations with declaration-order visibility,
   specialized constraints, nominal identity, and source provenance.
6. **Complete.** Add generated function/procedure declarations, bodies, and bounded
   instantiations with local overload visibility and scope-qualified identity.
7. **Complete.** Complete generated constants, signals, aliases, components, package
   instantiations, nested items, and collision/unsupported-item diagnostics.
8. **Complete.** Complete architecture/block/generate/process/subprogram local declarative
   regions for bounded constants, types, objects, aliases, packages, and
   non-suspending local callables.
9. **Complete.** Complete remaining locally static `if`/`for`/`case` generate choices,
   including enumeration/character choices, groups, ranges, `others`,
   overlap/null handling, labels, hierarchy, and specialization identity.
10. **Complete.** Close focused positive/negative/runtime, sanitizer, source/catalog, full
    Debug/Release, matrix/docs, commit, and push evidence.

Batch status is **complete**. All ten tasks and their gates are closed, and the
current in-progress ten-task record has moved to Batch 115 below.

Tasks 1 and 2 are focused-complete. Guarded blocks retain their exact Boolean
expression, optional `is`, labels, and source span; a missing `)` reports
`FSIM-VHDL-PARSE-231`. Elaboration creates the scope-qualified implicit
`guarded_scope.guard` Boolean signal and reactive driver after qualifying parent
names, while `FSIM-ELAB-GEN-013` rejects a statically non-Boolean guard. The
focused frontend/elaboration evidence and interpreter plus LLVM O0/O2 cold/warm
application differential prove false-to-true activation, scheduling,
normalized VCD, hierarchy, and cache reuse. Batch 114 remains **in progress**
with Tasks 3 through 10 outstanding.

Task 3 is focused-complete. Block generic/port clauses and maps support
positional-then-named associations, declaration-ordered defaults, `open`, and
generic-dependent port constraints. Selected-unit generate expansion is now
delayed until enclosing value/type/subprogram/local-package specialization is
complete; the block binder then resolves value, type, function, procedure, and
package formals before its body and ports. Scoped owned signals and exact
enclosing-signal aliases coexist with stable scoped callable/package
identities and transitive provenance. Focused negative evidence covers
association/formal-mode/writable/profile failures plus missing type and
mismatched function, procedure, and package actuals. The merged application
test proves interpreter and LLVM O0/O2 cold/warm value/VCD agreement and a
block package-map edit that changes package identity, behavior, and the native
cache key. The final five-test focused gate passes in 15.91 seconds with 1,445
production diagnostics and all 344 authored sources within the 2,000-line
limit. Tasks 1 through 3 are focused-complete; Batch 114 remains **in progress**
with Tasks 4 through 10 outstanding.

Task 4 is focused-complete. Bounded constants, signals, and components before
`begin` are retained for conditional then/else, iterative, and labeled case
alternatives. Nonempty generate declarative parts now require the separating
`begin`; omission reports `FSIM-VHDL-PARSE-234`, while declaration-free bodies
retain their legal optional form. Expansion folds only the selected body's
declarations and qualifies them beneath its exact branch, alternative, or
`label[index]` scope. Focused elaboration plus the merged interpreter/LLVM
O0/O2 cold/warm/VCD differential prove conditional, selected-case, and
`lanes[2]` declarations. The final five-test focused gate passes in 16.10
seconds with 1,446 production diagnostics and all 344 authored sources within
the 2,000-line limit. Tasks 1 through 4 are focused-complete; Batch 114 remains
**in progress** with Tasks 5 through 10 outstanding.

Task 5 is focused-complete. Generated conditional, iterative, and labeled case
bodies retain bounded array, enumeration, record, and subtype declarations.
The type-resolution partition merges declarations by source offset, rejecting
forward type visibility while admitting same-spelled types in separate
alternatives. Selected-body expansion substitutes prior local constants and
the concrete loop index, qualifies every realized type under its exact branch
or `label[index]` path, and extends source-backed nominal identity with that
scope. Focused elaboration proves three different loop-dependent widths and
three distinct source-and-scope identities. The merged interpreter/LLVM O0/O2
cold/warm/VCD differential executes conditional, loop, and case generated
subtypes unchanged. The final five-test focused gate passes in 15.30 seconds
with 1,446 production diagnostics and all 345 authored sources within the
2,000-line limit. Tasks 1 through 5 are focused-complete; Batch 114 remains
**in progress** with Task 6 current and Tasks 7 through 10 pending.

Task 6 is focused-complete. Selected generate bodies retain ordinary VHDL
function/procedure declarations, conforming bodies, local overloads,
value-generic subprogram templates, and `is new` instances. Callable bodies see
only their own and earlier physical-source designators, while expansion merges
conforming declaration/body pairs and qualifies ordinary, template, and
instance names beneath the exact selected scope. A post-expansion bounded
generic materialization pass produces scoped instance identities and preserves
source dependencies. Focused negative evidence covers malformed purity,
duplicate profiles, ordinary forward calls, and forward generic instances. The
merged interpreter/LLVM O0/O2 application executes both ordinary and generic
function/procedure paths, proves cold/warm reuse, and changes behavior plus the
specialization key after one generated generic-map edit. The final five-test
focused gate passes in 15.66 seconds with 1,447 production diagnostics and all
346 authored sources within the 2,000-line limit. Tasks 1 through 6 are
focused-complete; Batch 114 remains **in progress** with Task 7 current and
Tasks 8 through 10 pending.

Task 7 is focused-complete. Selected conditional, case-alternative, iterative,
block, and recursively nested bodies retain the bounded constant, signal,
type/subtype alias, block-port alias, component, callable, and local
generic-package declaration families. Expansion scope-qualifies local-package
names and maps, excludes their lexical prefixes from premature external
package lookup, and reruns package materialization so selected constants,
types, callables, identities, and source provenance reach the concrete unit.
Direct, nested, and iteration-dependent package instances have focused HIR and
elaboration coverage. `FSIM-VHDL-SEM-081` deterministically rejects
cross-family homographs in physical source order, and
`FSIM-VHDL-UNSUPPORTED-053` targets recognized unsupported generated items.
The merged interpreter/LLVM O0/O2 application path executes a generated
package-selected constant, verifies its scoped package identity, normalized
VCD, and cold/warm cache reuse. The final five-test focused Release gate passes
in 14.68 seconds with 1,449 production diagnostics and all 346 authored sources
within the 2,000-line limit. Tasks 1 through 7 are focused-complete; Batch 114
remains **in progress** with Task 8 current and Tasks 9 and 10 pending.

Task 8 is focused-complete. Architecture, generated/block, process, ordinary
subprogram, and instantiated generic-subprogram regions retain their bounded
constants, types/subtypes, variables/signals, typed object aliases, local
generic packages, and nested non-suspending callables. Local constants are
specialized in declaration order; generic-template locals wait for their own
actuals. Recursive source-ordered materialization qualifies packages, aliases,
and callables, hoists executable nested callables, then repeats local-package
materialization after generic instantiation. Nested type/provenance visitors
cover the complete retained graph. `FSIM-VHDL-SEM-082`/`083`,
`FSIM-VHDL-PARSE-236`, and `FSIM-VHDL-UNSUPPORTED-054` give deterministic
collision and malformed-alias failures. The merged interpreter/LLVM O0/O2
application differential executes architecture/generated and callable aliases,
ordinary/generic local packages, local functions/procedures, cold/warm cache
reuse, normalized VCD, and edit-sensitive specialization identity. The final
five-test focused Debug/Release gates pass in 15.93/15.12 seconds with 1,453
production diagnostics and all 347 authored sources within the 2,000-line
limit. Tasks 1 through 8 are focused-complete;
Batch 114 remains **in progress** with Task 9 current and Task 10 pending.

Task 9 is focused-complete. Selection-generate elaboration derives a retained
enumeration selector domain from nominal metadata or a directly named generic,
port, or signal and maps identifier/character literals plus static constants
to ordinals. Grouped choices, ascending/descending ranges, null ranges, and
`others` retain exact labels and hierarchy; unknown or nominally mismatched
choices, duplicate defaults, and overlapping integer/enumeration intervals use
the stable `FSIM-ELAB-GEN-008`/`009`/`010` family. Frontend, elaboration, and
the merged interpreter/LLVM O0/O2 differential prove the labeled
`selected.selected_value` path, execution/process count, cold/warm reuse, and
an enum-default edit that changes both selected behavior and specialization
identity. The final five-test focused Debug/Release gates pass in 15.81/15.14
seconds with 1,453 production diagnostics and all 348 authored sources within
the 2,000-line limit. Tasks 1 through 9 are focused-complete; Batch 114 remains
**in progress** with Task 10 current.

Task 10 is complete. Exact LLVM 22.1.8 warnings-as-errors Debug passed all 66
tests in 183.65 seconds, including the merged application in 16.03 seconds and
`fsim.application.scoped_locals` in 0.89 seconds. Release passed all 66 in
161.11 seconds, including the merged application in 14.84 seconds and scoped
locals in 0.82 seconds. The LLVM-disabled ASan/UBSan focused six-test gate
passed in 46.77 seconds with LeakSanitizer disabled only because the managed
ptrace environment cannot start it. The diagnostic catalog retains 1,453
production codes and all 348 authored sources pass the 2,000-line gate.
Feature-matrix row VH-229 and the language/diagnostic documentation record the
completed selector-domain contract. All ten Batch 114 tasks are complete.

## Completed feature batch 115

1. **Complete.** Audit and retain every remaining synthesizable sequential and concurrent
   statement form in typed HIR, with exact labels, spans, and targeted
   unsupported-form diagnostics.
2. **Complete.** Complete sequential signal/variable assignments, procedure calls, `null`,
   conditionals, loops, and case statements across nested labeled scopes.
3. **Complete.** Add VHDL-2008 matching case statements and matching selected/conditional
   assignments with exact wildcard semantics and deterministic legality checks.
4. **Complete.** Complete discrete case choices with grouped literals, locally static ranges,
   `others`, null ranges, overlap, duplicate, and coverage diagnostics.
5. **Complete.** Complete concurrent simple, conditional, and selected signal assignments,
   including guarded/delay-mechanism interaction and driver identity.
6. **Complete.** Preserve process, loop, case-alternative, and labeled statement scopes in
   hierarchy, name lookup, debugger metadata, and specialization provenance.
7. **Complete.** Lower dynamic packed/composite indices, slices, and chained selections for
   supported expression and assignment targets with checked bounds and direction.
8. **Complete.** Complete sensitivity, scheduling, delta/update ordering, and exact
   interpreter/LLVM behavior for the newly retained statement and selection forms.
9. **Complete.** Prove positive/negative parser and elaboration coverage plus interpreter,
   LLVM O0/O2, cache-edit, hierarchy, debugger, and normalized-VCD differentials.
10. **Complete.** Update matrix/diagnostics/docs and pass sanitizer, source/catalog, full
    Debug/Release, commit, and push gates before closing Batch 115.

Batch status is **complete**. Keep this exact completed ten-task list retained
in both the official plan and this handoff while Batch 116 is current.

Task 1 is focused-complete. Generalized sequential labels retain canonical
identity and full spans on every currently retained assignment, call, report,
wait, `null`, conditional, case, and loop HIR node. Sequential selected signal
and variable assignments retain their distinct assignment kinds; labeled
simple/conditional/selected assignments and procedure calls are also retained
in architecture and generated regions. `process(all)` becomes wildcard
sensitivity, while `FSIM-VHDL-SEM-084` checks `if`/`case`/process end labels
and `FSIM-VHDL-SEM-085` rejects mixing `all` with explicit names. The final
five-test focused Debug/Release gates pass in 16.21/15.30 seconds with 1,455
production diagnostics and all 348 authored sources within the 2,000-line
limit. The late Batch 114 negative fixture is compacted from 2,010 to exactly
2,000 lines. Batch 115 remains **in progress** with Task 2 current and Tasks 3
through 10 pending.

Task 2 is focused-complete. The dedicated statement application executes a
process-local scalar procedure, selected variable assignment, labeled
ascending loop, nested labeled `if`/`else` and exact `case`, procedure calls,
`null`, a sequential selected signal assignment, and a labeled permanent wait.
The final integer value 10 agrees across interpreter and LLVM O0/O2 cold/warm
runs with one process and stable cache reuse. The final five-test focused
Debug/Release gates pass in 15.90/14.99 seconds with 1,455 production
diagnostics and all 348 authored sources within the 2,000-line limit. Batch
115 remains **in progress** with Task 3 current and Tasks 4 through 10 pending.

Task 3 is focused-complete. VHDL-2008 `case? ... end case?`, `select?`, and
matching equality `?=` retain distinct HIR and lower through the appended
`vhdl_match_equal` SimIR operator. Matching is symmetric for `-`, normalizes
0/L and 1/H, returns a known false result for other meta-value comparisons,
and is exact for bit values. Stable diagnostics reject inconsistent matching-
case end markers, non-bit/std_ulogic selector or operand domains, nonliteral or
wrong-width bounded choices, and overlapping wildcard patterns. Native-object
schema 70 prevents reuse across the added operator. The statement differential
now reaches integer 13 through matching case, selected-variable, and
conditional forms across interpreter and LLVM O0/O2 cold/warm runs with one
process. The final seven-test focused Debug/Release gates pass in 19.04/18.11
seconds; scoped locals remains quick at 0.84/0.83 seconds. The catalog covers
1,460 production diagnostics and all 350 authored sources remain within the
2,000-line limit. Batch 115 remains **in progress** with Task 4 current and
Tasks 5 through 10 pending.

Task 4 is focused-complete. Exact VHDL case and selected-assignment choices
retain grouped literals plus ascending and descending discrete-range HIR.
Elaboration accepts locally static integer, Boolean, enumeration, and packed
choices of the selector type; ignores null ranges; rejects wrong-type or
nonstatic choices, subtype violations, duplicates, and overlaps; and proves
complete subtype coverage when `others` is absent. Range execution lowers to
checked signed or unsigned comparisons. The statement differential reaches
integer 14 through a descending range while a null range remains inert across
the interpreter and LLVM O0/O2 cold/warm paths. The final five-test focused
Debug/Release gates pass in 15.83/15.19 seconds, and scoped locals remains
quick at 0.85/0.88 seconds. All 1,466 production diagnostics are cataloged and
all 352 authored sources remain within the 2,000-line limit. Batch 115 remains
**in progress** with Task 5 current and Tasks 6 through 10 pending.

Task 5 is focused-complete. Concurrent simple, conditional, and selected
signal assignments retain `guarded`, their exact delay mechanism, and explicit
`null` waveform elements in typed HIR. Generated-block expansion binds them to
the implicit Boolean `GUARD`, while one reactive process per concurrent
statement preserves distinct driver identity. Within the bounded nine-state
target subset, false guards and explicit-null alternatives schedule a
resolution-neutral `Z` transaction through the same projected-waveform path;
other domains and missing enclosing guards receive stable diagnostics. The
merged application combines three base and three guarded drivers and observes
`XXX` after activation, `X11` after explicit-null selection, and the base
values again after deactivation across interpreter and LLVM O0/O2 execution.
The final five-test focused Debug/Release gates pass in 16.88/16.26 seconds;
scoped locals remains quick at 0.82/0.83 seconds. All 1,469 production
diagnostics are cataloged and all 354 authored sources pass the 2,000-line
gate. Batch 115 remains **in progress** with Task 6 current and Tasks 7
through 10 pending.

Task 6 is focused-complete. SimIR debug points and runtime execution points
carry canonical process-rooted lexical scope paths for labeled statements,
labeled or anonymous loops, and source-stable case alternatives. Debugger
hierarchy navigation retains each scope and ancestor, while signal/string/
container lookup walks outward from a selected statement scope. The statement
application navigates `worker.iterations.choice` and resolves architecture
signal `observed` through that parent search. Native-object schema 71 includes
the execution scope in specialization provenance, with a dedicated scope-only
cache miss. The final six-test source/runtime/elaboration/LLVM/application/
scoped-locals gates pass in 19.47/19.01 seconds for Debug/Release; scoped
locals remains quick at 0.82/0.81 seconds. All 1,469 production diagnostics
are cataloged and all 354 authored sources pass the 2,000-line gate. Batch 115
remains **in progress** with Task 7 current and Tasks 8 through 10 pending.

Task 7 is focused-complete. Fixed-width dynamic VHDL slices lower both
integer-family bounds through declared-range and direction-aware exact-length
checks before reusing common dynamic-part SimIR. Ascending/descending reads,
process-variable targets, and packed-record member chains preserve exact
four-/nine-state values; stable diagnostics cover incompatible source
direction/profile, noninteger bounds, unknown assignment width, and another
selection after a runtime slice. Runtime bounds outside the declared range or
with the wrong direction/length fail deterministically. The condition lowerer
now has its own source partition, keeping every authored file within the hard
limit. Focused diagnostic/source/elaboration/LLVM/runtime/scoped-locals gates
pass in 4.09/3.93 seconds for Debug/Release, with scoped locals at 0.83/0.82
seconds. All 1,472 production diagnostics are cataloged and all 356
authored sources pass the 2,000-line gate. Batch 115 remains **in progress**
with Task 8 current and Tasks 9 through 10 pending.

Task 8 is focused-complete. Single and atomic multi-element projected signal
waveforms accept checked fixed-width dynamic VHDL slices through the existing
dynamic projected operations. The explicit right bound anchors the normalized
offset for ascending and descending targets, capturing the selected region at
assignment execution while retaining driver identity, delta/update order,
transport/inertial transaction behavior, and exact Logic4/Logic9 values.
Concurrent assignment sensitivity now includes dynamic target bounds but not
the written signal itself. Interpreter and LLVM O0/O2 tests cover multi-bit
single/waveform callbacks; native-object schema 72 records the widened
contract without an ABI change. The assertion lowerer is now a separate source
partition. Eight focused diagnostic/source/elaboration/LLVM/runtime/projected/
Logic9/scoped-locals tests pass in 4.44/4.24 seconds for Debug/Release, with
scoped locals at 0.85/0.81 seconds. All 1,472 diagnostics are cataloged and all
357 authored sources pass the line gate. Batch 115 remains **in progress**
with Task 9 current and Task 10 pending.

Task 9 is focused-complete. Positive and negative frontend/elaboration cases
cover ascending/descending, packed-record-member, read/local/signal target,
single/multi projected waveform, malformed syntax, direction/type, and runtime
length behavior. The merged VHDL array application proves dynamic slices in
debug-visible locals and projected signals across interpreter and LLVM O0/O2
cold/warm runs, package-source cache invalidation, hierarchy/debugger lookup,
normalized VCD, exact Logic9 values, and identical O0/O2 runtime-failure
messages. Seven focused diagnostic/source/elaboration/LLVM/application/runtime/
scoped-locals tests pass in 4.99/4.67 seconds for Debug/Release; the array case
is 0.85/0.78 seconds and scoped locals is 0.83/0.80 seconds. All 1,472
diagnostics and 357 authored sources remain gated. Batch 115 remains **in
progress** with Task 10 current.

Task 10 is complete. Feature-matrix row VH-230, the architecture and language
support contracts, and the evidence inventory record fixed-width dynamic VHDL
slice reads, local writes, projected signal waveforms, bounds, sensitivity,
cache, hierarchy, debugger, Logic9, and normalized-VCD behavior. The full gate
also corrected the Tcl debugger regression to expect the process scope added
by Task 6. The LLVM-disabled ASan/UBSan focused seven-test gate passed in 3.91
seconds. Exact LLVM 22.1.8 warnings-as-errors Debug passed all 66 tests in
182.10 seconds, with scoped locals at 0.83 seconds and VHDL arrays at 0.84
seconds; Release passed all 66 in 160.13 seconds, with scoped locals at 0.82
seconds and VHDL arrays at 0.80 seconds. All 1,472 production diagnostics are
cataloged and all 357 authored sources pass the 2,000-line gate. All ten Batch
115 tasks are complete.

## In-progress feature batch 116

1. **Complete.** Audit and retain multidimensional and composite-element array declarations,
   constraints, objects, aggregates, selections, ports, and callable boundaries
   in typed HIR with exact spans and targeted diagnostics.
2. **Complete.** Complete type/subtype layout for multidimensional and composite arrays,
   preserving every index range, direction, null range, element subtype, nominal
   identity, and deterministic flattened storage mapping.
3. **Complete.** Complete contextual array aggregates with positional, named, discrete-range,
   choice-list, and final `others` associations, including nested aggregates,
   coverage, overlap, duplicate, and subtype legality.
4. **Complete.** Lower multidimensional indexing, slicing, and supported chained selections for
   reads and assignment targets with checked ordinal mapping, bounds, direction,
   and shape compatibility.
5. **Complete.** Execute null arrays and slices through object initialization, aggregates,
   assignments, loops, copies, equality, debugger inspection, and trace behavior
   without allocating or updating phantom elements.
6. **Complete.** Complete same-language entity/component port and generic boundaries for
   multidimensional and composite arrays with exact constraint adaptation,
   aliases, copy direction, driver ownership, and specialization identity.
7. **Complete.** Complete function/procedure parameter, result, local, package, and generated
   callable boundaries for supported array shapes with deterministic copy-in,
   copy-out, return, lifetime, and provenance behavior.
8. **Complete.** Complete sensitivity inference, partial/composite signal scheduling, driver
   resolution, delta/update ordering, and interpreter/LLVM parity for array
   element and slice targets.
9. **Complete.** Prove positive/negative parser and elaboration coverage plus interpreter,
   LLVM O0/O2, cache-edit, hierarchy, debugger, normalized-VCD, null-range, port,
   and callable differentials.
10. **Complete.** Update matrix/diagnostics/docs and pass sanitizer, source/catalog, full
    Debug/Release, commit, and push gates before closing Batch 116.

Batch status is **complete**. This exact completed ten-task list is retained in
both the official plan and this handoff now that work has moved to Batch 117.

Task 1 is focused-complete. Typed HIR now retains every source-ordered
integer-family array dimension, mixed constrained/unconstrained ranges, the
complete direct or named composite element type, and multidimensional subtype
constraints on objects and ports. Nested aggregates, multi-index reads,
subarray-slice operands, and array-typed function/procedure boundaries retain
their exact expression and source-span structure. Existing one-dimensional
execution reads a compatibility mirror of the first dimension. At this
checkpoint `FSIM-ELAB-VHARRAY-008` staged multidimensional types behind the
then-pending Task 2 layout. The richer recursive Type layout exposed and
repaired an O3 GCC
optional-profile constructor false positive without a warning suppression.
Final frontend/catalog/source/elaboration/type-generic/component/array/scoped-
locals gates pass in 3.18/3.05 seconds for Debug/Release, with scoped locals
at 0.86/0.83 seconds. All 1,473 diagnostics are cataloged and all 357 authored
sources remain within the 2,000-line gate.

Task 2 is focused-complete. Concrete array layout now preserves every evaluated
dimension range, direction, and null state, records rightmost-fastest packed-bit
strides, and computes a checked total width without erasing nominal array or
element identity. Scalar, vector, record, nested-concrete, constrained-open,
and interface-type-generic element paths share the layout engine; one-dimensional
arrays keep their declared packed-range compatibility mirror. Rank mismatch,
illegal reconstraint, index-base, indefinite-element, and overflow failures
remain deterministic. The layout engine is structurally partitioned in
`elaboration_vhdl_array_layout.cpp`; all 358 authored sources remain within the
2,000-line gate, with `elaborator_internal.hpp` at exactly 2,000 lines. Focused
Debug and Release seven-test gates passed in 2.29 and 2.08 seconds, with scoped
locals at 0.86/0.83 seconds and VHDL arrays at 0.95/0.80 seconds. The
LLVM-disabled ASan/UBSan gate passed the same seven tests in 4.25 seconds. All
1,473 production diagnostics remain cataloged. Batch 116 remains **in
progress** with Task 3 current and Tasks 4 through 10 pending.

Task 3 is focused-complete. The contextual aggregate lowerer now walks one
source dimension at a time, using its exact range and packed-bit stride to map
positional, discrete, range, choice-list, and final-`others` associations.
Nested multidimensional subaggregates, nested named arrays, vector values, and
nominal record-element aggregates lower recursively with exact width,
two-/four-/nine-state, coverage, overlap, duplicate, bounds, and record-subtype
checks. Application evidence covers concurrent assignments, conditional
alternatives, process-local initialization, interpreter execution, LLVM O0/O2,
cold/warm cache reuse, and package-edit invalidation. Focused Debug and Release
seven-test gates passed in 2.20 and 2.21 seconds, with VHDL arrays at 0.94/0.93
seconds and scoped locals at 0.80/0.83 seconds. The LLVM-disabled ASan/UBSan
gate passed the same seven tests in 4.33 seconds. All 1,473 production
diagnostics are cataloged and all 358 authored sources remain within the
2,000-line gate. Batch 116 remains **in progress** with Task 4 current and Tasks
5 through 10 pending.

Task 4 is focused-complete. Multidimensional calls and comma-separated targets
normalize into source-ordered selection chains. Static and runtime reads and
local/signal targets use the concrete rightmost-fastest dimension strides;
runtime indices are signed-32-bit checked, converted to right-relative packed
ordinals, and combined before fixed-width extract/insert operations. Static and
runtime partial-dimensional slices retain exact direction and contextual shape,
while nested named arrays, vector elements/slices, and nominal record elements
remain typed through supported chains. Concurrent sensitivity discovery now
retains multi-index array prefixes. Positive frontend/application evidence
covers scalar, vector, nested-array, and record elements, local and signal
targets, interpreter execution, LLVM O0/O2, cold/warm cache reuse, and package
edits; negative evidence covers type, bounds, direction, shape, and identical
interpreter/compiled runtime range failures. Focused Debug and Release
seven-test gates passed in 2.46 and 2.30 seconds, with VHDL arrays at 1.18/1.14
seconds and scoped locals at 0.90/0.81 seconds. The LLVM-disabled ASan/UBSan
five-test gate passed in 4.51 seconds after exposing and repairing a selected-
type lifetime defect. All 1,477 production diagnostics are cataloged and all
359 authored sources remain within the 2,000-line gate. Batch 116 remains **in
progress** with Task 5 current and Tasks 6 through 10 pending.

Task 5 is focused-complete. Concrete null VHDL array signals and
locals now retain their declared ranges and nominal types while using true
zero-bit packed values. Null initialization, contextual `others` aggregates,
whole-object and null-slice assignments, copies, equality/inequality,
zero-iteration `'range` loops, and `'length = 0` lower without emitting phantom
loads, writes, or scheduled updates. Interpreter and LLVM debugger reads render
the values as `<null>`; application tracing omits zero-width declarations because
VCD has no legal zero-width net. The merged VHDL array application covers
interpreter and LLVM O0/O2 execution, cold/warm cache reuse, package-edit
invalidation, debugger inspection, and application VCD behavior. The focused
eight-test Debug and Release gates each passed in 3.04/2.96 seconds, with VHDL
arrays at 1.40/1.40 seconds and scoped locals at 0.87/0.93 seconds. The
LLVM-disabled ASan/UBSan six-test gate passed outside the ptrace sandbox in
2.37 seconds. All 1,477 production diagnostics are cataloged and all 359
authored sources remain within the 2,000-line gate. Batch 116 remains **in
progress** with Task 6 current and Tasks 7 through 10 pending.

Task 6 is focused-complete. Same-language VHDL entity and component boundaries
now adapt unconstrained multidimensional and composite-array formals to the
actual signal's exact ranges, directions, null state, flattened strides, and
element profile before specialization. Port aliases retain the parent signal
ID and existing driver ownership, while the adapted shape participates in a
dedicated specialization identity. Component compatibility and the
`vhdl-component-binding-v7` identity now profile every array dimension,
generic-dependent constraint, flattened width, nested element type, and record
member; equal-width arrays with different shapes can no longer collide or bind
silently. The merged VHDL array application covers direct-entity,
component-bound, and generic-constrained multidimensional/composite ports,
input/output copies, signal aliases, equal-width/different-shape cache keys,
interpreter and LLVM O0/O2 execution, cold/warm reuse, and package-edit
invalidation. Negative evidence reports `FSIM-ELAB-BIND-031` for an incompatible
direct boundary and `FSIM-ELAB-VHCOMP-007` for an incompatible component/entity
profile. Focused eight-test Debug and Release gates passed in 5.57 and 5.53
seconds, with VHDL arrays at 1.51/1.46 seconds and scoped locals at 0.86/0.83
seconds. The LLVM-disabled ASan/UBSan seven-test gate passed outside the ptrace
sandbox in 4.71 seconds. All 1,477 production diagnostics are cataloged and all
361 authored sources remain within the 2,000-line gate. Batch 116 remains **in
progress** with Task 7 current and Tasks 8 through 10 pending.

Task 7 is focused-complete for concrete supported array shapes. Callable
overload matching now requires same-nominal VHDL arrays to have compatible
rank, element subtype, bounds, directions, null state, and flattened strides;
same-width but differently constrained multidimensional actuals no longer pass
profile selection. The merged VHDL array application executes constrained
multidimensional and record-element arrays through package function parameters,
locals, and results; package procedure constant/inout parameters and
deterministic copy-in/copy-out; nested local function calls; and a function
declared inside a selected generate body. Interpreter and LLVM O0/O2 runs,
cold/warm cache reuse, package-edit invalidation, and signal-value comparisons
cover return lifetime and source provenance. Negative evidence reports
`FSIM-ELAB-VHOVER-002` for an incompatible callable shape. Focused eight-test
Debug and Release gates passed in 5.88 and 5.75 seconds, with VHDL arrays at
1.88/1.76 seconds and scoped locals at 0.83/0.81 seconds. The LLVM-disabled
ASan/UBSan seven-test gate passed outside the ptrace sandbox in 5.79 seconds.
All 1,477 production diagnostics are cataloged and all 361 authored sources
remain within the 2,000-line gate. Batch 116 remains **in progress** with Task
8 current and Tasks 9 through 10 pending.

Task 8 is focused-complete. Lowered processes now retain explicit static packed
driver regions; whole and runtime-selected targets remain conservatively marked
as whole-object drivers. VHDL unresolved multidimensional/composite arrays may
therefore use disjoint process-owned elements or slices while overlapping
regions still report `FSIM-ELAB-DRV-001`; the existing SystemVerilog variable
single-process rule is unchanged. Resolved `std_logic` driver slots initialize
owned static regions to the subtype default and unrelated regions to neutral
`Z`, so disjoint partial drivers no longer inject phantom `U` values. Runtime
registration validates every retained region before execution. The merged VHDL
array application covers disjoint multidimensional rows, record-element array
updates from separate processes, resolved partial drivers, common update-phase
coalescing, chained-selection sensitivity, interpreter and LLVM O0/O2 parity,
cold/warm cache reuse, and package-edit invalidation; an overlapping-slice
design supplies negative elaboration evidence. Focused eight-test Debug and
Release gates passed in 6.18 and 5.93 seconds, with VHDL arrays at 2.02/1.89
seconds and scoped locals at 0.83/0.82 seconds. The LLVM-disabled ASan/UBSan
seven-test gate passed outside the ptrace sandbox in 5.99 seconds. All 1,477
production diagnostics are cataloged and all 362 authored sources remain
within the 2,000-line gate. Batch 116 remains **in progress** with Task 9
current and Task 10 pending.

Task 9 is focused-complete. The merged VHDL array application now places
hierarchy-port copies, package-callable results, disjoint resolved and
unresolved partial drivers, composite array elements, and chained-selection
sensitivity values in the same normalized custom-VCD stream used for exact
interpreter/LLVM and cold/warm comparisons. Debugger evidence covers the same
hierarchy, callable, multidimensional resolved, and composite objects, while
the application VCD proves their declarations and continues to omit the legal
zero-width null array. Existing positive parsing and elaboration are paired
with aggregate/type/bounds/direction/shape, dynamic range, hierarchy-port,
component-profile, callable-profile, and overlapping-driver negative cases;
runtime failures are compared exactly between interpreter and compiled modes.
Both O0/O2 loops retain package-edit cache invalidation and distinct equal-
width hierarchy specialization keys. Focused eight-test Debug and Release
gates passed in 6.11 and 5.84 seconds, with VHDL arrays at 2.04/1.92 seconds and
scoped locals at 0.84/0.81 seconds. The LLVM-disabled ASan/UBSan seven-test gate
passed outside the ptrace sandbox in 5.10 seconds. All 1,477 production
diagnostics are cataloged and all 362 authored sources remain within the
2,000-line gate. Batch 116 remains **in progress** with Task 10 current.

Task 10 is complete. Feature-matrix rows VH-231 through VH-238 and the compact
language-support contract now record bounded multidimensional/composite array
HIR, layout, aggregates, selections, null values, hierarchy and callable
boundaries, partial drivers, sensitivity, and differential evidence. The full
gate found and repaired missing static-expression traversal of retained array
dimension constraints, then updated legacy resolved partial-driver expectations
to the neutral `Z` values required outside each owned region. The
LLVM-disabled ASan/UBSan eleven-test frontend/catalog/source/elaboration,
expression, scoped-local, overload, component, array, projected-waveform, and
runtime gate passed outside the ptrace sandbox in 10.87 seconds. Exact LLVM
22.1.8 warnings-as-errors Debug passed all 66 tests in 182.13 seconds, with
VHDL arrays at 1.97 seconds and scoped locals at 0.84 seconds; Release passed
all 66 in 160.21 seconds, with arrays at 1.91 seconds and scoped locals at 0.84
seconds. All 1,477 production diagnostics are cataloged and all 362 authored
sources pass the 2,000-line gate. All ten Batch 116 tasks are complete.

### Batch 117 — VHDL nested composites and expression closure — Complete

The current ten implementation tasks are:

1. **Complete.** Audit and retain nested record/enumeration declarations, qualified
   expressions, aggregate choice forms, composite attributes, and composite
   operations in typed HIR with exact spans and targeted diagnostics.
2. **Complete.** Complete recursive bounded record layout and legality for nested record,
   array, enumeration, vector, and scalar members with nominal identity,
   defaults, constraints, and deterministic flattened storage.
3. **Complete.** Complete enumeration visibility and overload candidate behavior inside nested
   composites, aggregates, selections, comparisons, choices, conversions, and
   hierarchy/callable profiles.
4. **Complete.** Lower VHDL qualified expressions and supported subtype conversions with exact
   contextual type, constraint, state-domain, bounds, and nominal checks.
5. **Complete.** Complete record and array aggregate element-choice, range, choice-list,
   qualified, nested, and final `others` forms with exact order, coverage,
   overlap, duplicate, and subtype legality.
6. **Complete.** Complete scalar and composite type/object attributes across nested records,
   arrays, and enumerations, including static folding, executable results,
   dimensions, bounds, ranges, positions, and checked failures.
7. **Complete.** Complete supported composite equality, inequality, matching, concatenation,
   selection, assignment, conditional/case choice, and conversion operations
   with interpreter/LLVM parity.
8. **Complete.** Complete nested-composite hierarchy ports, generic and callable boundaries,
   aliases/copies, driver ownership, sensitivity, scheduling, debugger/VCD,
   provenance, and specialization/cache identity.
9. **Complete.** Prove positive/negative parser and elaboration coverage plus interpreter,
   LLVM O0/O2, cold/warm/edit, hierarchy, callable, debugger, normalized-VCD,
   null/constraint, and exact-failure differentials.
10. **Complete.** Update matrix/diagnostics/docs and pass sanitizer, source/catalog, full
    Debug/Release, commit, and push gates before closing Batch 117.

Batch status is **in progress**. Keep this exact ten-task list current in both
the official plan and this handoff. Change it to complete only after all ten
tasks and their gates close and work moves to Batch 118.

Task 1 is focused-complete. VHDL record declarations now retain named record,
array, enumeration, and other composite member subtype indications recursively
in `PackedMember::nested_types` with exact member and type-name spans instead
of discarding them as unsupported syntax. The frontend fixture combines
record-of-record, record-of-array, record-of-enumeration, and array-of-record
declarations with qualified nested record/array aggregates, discrete and
`others` choices, composite equality, and a composite type attribute. Integer
record members retain targeted `FSIM-VHDL-UNSUPPORTED-026` rejection while
their later bounded type families remain outside this task. Focused nine-test
Debug and Release gates passed in 3.61 and 3.35 seconds, with VHDL arrays at
2.04/1.86 seconds and scoped locals at 0.86/0.81 seconds. The LLVM-disabled
ASan/UBSan nine-test gate passed outside the ptrace sandbox in 5.93 seconds.
All 1,477 production diagnostics are cataloged and all 362 authored sources
remain within the 2,000-line gate. Batch 117 remains **in progress** with Task
2 current.

Task 2 is focused-complete. Named record members now resolve recursively before
layout, propagate the strongest nested state domain, preserve nominal subtype
identity, and defer composite array-element width validation until the checked
layout pass has materialized its record element. Recursive defaults now descend
through records and arrays, retaining `U` for nine-state vector leaves and the
left/default ordinal for enumeration and two-state leaves. The focused
elaboration fixture proves a constrained subtype of an unconstrained
array-of-record, 6/12/20-bit record/array/envelope widths, exact nested member
offsets and nominal identities, and `UUUU00`-family default values. Indefinite,
unknown, and cyclic member types receive `FSIM-ELAB-VHRECORD-001`,
`FSIM-ELAB-VHTYPE-001`, and `FSIM-ELAB-VHTYPE-002`; recursive width overflow
is cataloged as `FSIM-ELAB-VHRECORD-002`. The default builder moved to
`elaboration_defaults.cpp`, restoring the hard source-size gate. Focused
ten-test Debug and Release gates passed in 3.68 and 3.50 seconds, with VHDL
arrays at 2.01/1.89 seconds and scoped locals at 0.83/0.83 seconds. The
LLVM-disabled ASan/UBSan ten-test gate passed outside the ptrace sandbox in
6.45 seconds. All 1,479 production diagnostics are cataloged and all 364
authored sources remain within the 2,000-line gate. Batch 117 remains **in
progress** with Task 3 current.

Task 3 is focused-complete. VHDL object-type lookup now descends retained
record members for nested enumeration selections, and component profile
matching uses the same declaration-aware traversal. Selected enum reads,
writes, comparisons, case choices, nested aggregates, and callable actuals
retain exact nominal type and subtype context. Ordinary calls whose first
actual is enum-typed no longer enter the apostrophe-attribute path. A selected
input component actual materializes a bounded expression driver by aliasing
the parent record signal and retaining its member suffix; whole-signal
connections retain direct aliases. The focused fixture disambiguates function,
procedure, and component overloads whose result/entity context cannot choose
the candidate, executes exact nested aggregate and selected-member values, and
rejects equality between distinct nested enumeration types with
`FSIM-ELAB-VHENUM-002`.

The twelve-test LLVM 22.1.8 Debug and Release gates passed in 9.86 and 9.34
seconds. Scoped locals remained quick at 0.82/0.80 seconds; VHDL arrays passed
in 1.96/1.82 seconds, components in 0.94/0.91 seconds, and overloads in
2.01/1.83 seconds. The LLVM-disabled ASan/UBSan eleven-test gate passed in
8.63 seconds with leak detection disabled because the managed ptrace sandbox
prevents LeakSanitizer initialization; the requested unsandboxed execution was
denied by environment policy. All 1,479 diagnostics remain cataloged and all
364 authored sources pass the 2,000-line gate, with
`lowerer_expression.cpp` exactly at 2,000 lines. `git diff --check` passes.
Batch 117 remains **in progress** with Task 4 current and is not a CI-inspection
boundary.

#### Clean-context restart checkpoint — 2026-08-02

The live branch is `codex/resumable-jit`. Local `HEAD` and
`origin/codex/resumable-jit` both resolve to
`87eff95d800d88c2221f6a309acf1c54c38df86e`. Preserve the intentional,
unstaged Task 3 checkpoint in these ten modified files:

- `docs/implementation-plan.md`;
- `docs/v1-resume.md`;
- `src/elaboration/elaboration_constants.cpp`;
- `src/elaboration/elaboration_vhdl_components.cpp`;
- `src/elaboration/hierarchy_ports.cpp`;
- `src/elaboration/hierarchy_types.cpp`;
- `src/elaboration/lowerer_assignment.cpp`;
- `src/elaboration/lowerer_expression.cpp`;
- `src/elaboration/lowerer_process.cpp`; and
- `tests/elaboration/elaborator_vhdl_composite_test.cpp`.

Do not reset these files to the pushed base. Their validated contents are the
Task 3 implementation and evidence described immediately above. The
workspace-write sandbox keeps `.git` read-only, but on 2026-08-02 the user
installed a deterministic outside-sandbox allow rule for `git add`,
`git commit`, and `git push`. The exact ten-path `git add` above then completed
without a prompt, restoring the routine checkpoint workflow while retaining
the sandbox for other commands. The subsequent Task 3 commit and push establish
the clean continuation base; on restart, verify local and remote identity and
preserve a newer intentional checkpoint if present. Do not inspect CI because
Batch 117 is not a tenth-batch boundary.

Task 4 has been audited but no Task 4 implementation edits have begun. The
parser already represents `Type'(expression)` as a one-operand call named
`@vhdl-qualified:<canonical-type>`, while `Type(expression)` remains an
ordinary call. Current lowering treats qualification and conversion nearly
identically: it lowers directly in the target width and type, resizes, copies
between state domains, and applies only the existing enum/integer runtime
checks. This can incorrectly truncate qualified values or admit incompatible
nominal, scalar-domain, array-shape, record, and enumeration conversions.

Before changing behavior, structurally move the complete VHDL qualification/
conversion lowering block out of the exactly 2,000-line
`lowerer_expression.cpp` into a new
`src/elaboration/lowerer_vhdl_conversion.cpp`. Declare the narrow private
helper in `elaborator_internal.hpp`, call it from primary expression lowering,
and register the source in CMake. Then separate qualification-as-context from
conversion semantics and enforce exact nominal/base identity, subtype bounds,
array dimensions/direction/element compatibility, record compatibility,
state-domain legality, and scalar width. Add dedicated qualified-expression
and conversion diagnostics rather than overloading
`FSIM-ELAB-VHOVER-007`.

Add the focused matrix in a new
`tests/elaboration/elaborator_vhdl_conversion_test.cpp` and register it in the
test support, main, and CMake inventory. Positive evidence must cover enum and
subtype qualification, bounded array/string and nested-record aggregates,
integer/natural/positive conversions, same-base enum and array subtype
conversions, and contextual assignment/call/return use. Negative evidence must
cover invisible type marks, wrong nominal type or width, incompatible state
domains, unrelated enum/record/array types, indefinite or wider-than-64-bit
targets, out-of-subtype values, and wrong array rank, bounds, direction, or
element type. Reuse the existing array-shape and callable-type compatibility
helpers where their contracts match. Run every local build with at least eight
workers, retain the quick `fsim.application.scoped_locals` check, and keep
Task 4 **in progress** until its focused Debug, Release, sanitizer, catalog,
source-size, and differential evidence is recorded.

Task 4 is focused-complete. Qualification/conversion lowering now lives in the
dedicated 266-line `lowerer_vhdl_conversion.cpp`, leaving
`lowerer_expression.cpp` at 1,925 lines while the narrow internal declaration
keeps `elaborator_internal.hpp` at the 2,000-line limit. Qualified expressions
use their type mark as context without truncation or cross-domain copies;
supported conversions require integer-family compatibility or an exact
bounded nominal, array-shape, direction, element-profile, record,
enumeration, width, and state-domain match. Contextual VHDL integer and logic
array literals now retain their selected execution domain, and integer-family
qualification/conversion results participate in assignment type analysis.
The seven dedicated `FSIM-ELAB-VHQUAL-*` and `FSIM-ELAB-VHCONV-*`
diagnostics replace the former generic `FSIM-ELAB-VHOVER-007` path.

The focused fixture executes enum/subtype, integer/natural/positive,
sibling-array-subtype, logic-vector, nested-record, assignment, call, and
qualified-return cases. It rejects invisible and indefinite or oversized type
marks, wrong scalar width, state domain, nominal enum/record identity, array
rank, bounds, direction, and element type, contextual result mismatch, and
out-of-range integer/enumeration subtype values. The eight-worker LLVM 22.1.8
Debug and Release 12-test gates passed in 8.00 and 7.72 seconds, with
elaboration at 0.17/0.12 seconds, arrays at 2.14/2.05 seconds, and scoped
locals at 0.92/0.87 seconds. The LLVM-disabled ASan/UBSan 12-test gate passed
in 9.86 seconds with leak detection disabled because the managed ptrace
sandbox prevents LeakSanitizer initialization. All 1,485 production
diagnostics are cataloged and all 366 authored sources pass the 2,000-line
gate. Batch 117 remains **in progress** with Task 5 current and is not a
CI-inspection boundary.

Task 5 is focused-complete. Record aggregates now accept ordered element-name
choice lists, diagnose overlap at the exact repeated choice span, and retain
positional/named/choice-list/final-`others` coverage semantics. Record and
array elements are checked against their exact contextual nominal subtype and
state domain before insertion, with integer and enumeration subtype checks
emitted for runtime values. Qualified nested record and array aggregates flow
through the same contextual path instead of being rejected as non-record
objects. Bounded built-in `bit_vector`/`std_logic_vector` aggregate contexts
materialize an exact one-dimensional array profile so discrete, range,
choice-list, coverage, and `others` logic is shared with declared array types.
`FSIM-ELAB-VHAGG-009` and `FSIM-ELAB-VHARRAYAGG-009` distinguish contextual
subtype/domain failures from width and state-loss diagnostics.

The new 288-line focused fixture proves record choice-list and final-`others`
ordering; nested qualified records, arrays, and array-of-record values;
declared and built-in vector ranges and choice lists; exact interpreter
layouts; overlap, outside, missing, unknown, discrete-record, nominal, and
domain failures with source spans; and runtime enumeration-subtype rejection.
The eight-worker LLVM 22.1.8 Debug and Release 11-test gates passed in 7.88 and
7.13 seconds, with elaboration at 0.16/0.11 seconds, arrays at 2.27/1.98
seconds, record aggregates at 0.16/0.16 seconds, and scoped locals at
0.87/0.85 seconds. The LLVM-disabled ASan/UBSan 11-test gate passed in 10.14
seconds with leak detection disabled because the managed ptrace sandbox
prevents LeakSanitizer initialization. All 1,487 production diagnostics are
cataloged and all 367 authored sources pass the 2,000-line gate;
`lowerer_expression.cpp` remains within the limit at 1,993 lines. Batch 117
remains **in progress** with Task 6 current and is not a CI-inspection
boundary.

Task 6 is focused-complete. Executable VHDL attribute lowering is centralized
in the dedicated 466-line `lowerer_vhdl_attributes.cpp`. Built-in integer,
natural, positive, Boolean, and bit type marks are always visible;
integer-family, Boolean, bit, and declared enumeration bounds, direction,
length, position/value, adjacency, and checked successor/predecessor results
retain their exact domains. Scalar `range`/`reverse_range` loops preserve the
integer, Boolean, bit, or nominal enumeration loop-parameter type. Concrete
array type, subtype, signal, local, and nested record-selected object prefixes
query every locally static dimension in the complete rank, including null
ranges. Static folding evaluates pre-layout multidimensional constraints
instead of falling back to a flattened first-dimension packed range.

The 249-line elaboration fixture covers scalar bounds/positions, Boolean/bit
results, both dimensions and directions, nested record array objects, null
lengths, architecture-constant folding, scalar/array/enumeration range loops,
invalid ranks, dynamic dimensions, indefinite arrays, prefix legality, scalar
range misuse, and static/runtime checked failures. The new 236-line
application differential proves interpreter and LLVM O0/O2 cold/warm equality
plus dynamic scalar-bound failure parity. The eight-worker seven-test Debug
and Release gates passed in 3.86 and 4.14 seconds, with attributes at
0.16/0.17 seconds, arrays at 1.89/2.14 seconds, enumerations at 0.62/0.63
seconds, and scoped locals at 0.84/0.88 seconds. The LLVM-disabled ASan/UBSan
seven-test gate passed in 6.54 seconds with leak detection disabled under the
managed ptrace sandbox. The catalog covers 1,490 production diagnostics and
all 370 authored sources pass the 2,000-line gate;
`lowerer_expression.cpp` is 1,984 lines. Batch 117 remains **in progress**
with Task 7 current and is not a CI-inspection boundary.

Task 7 is focused-complete. The dedicated 456-line
`lowerer_vhdl_composite_operations.cpp` now owns bounded VHDL record/array
comparison and contextual concatenation plus nominal composite-assignment
validation. Equality and inequality require one record or array base and exact
recursive element profiles; differently constrained arrays of that base
compare by sequence length instead of being resized or rejected. Matching
equality and the newly retained matching inequality stay restricted to bit or
`std_ulogic` scalars and one-dimensional arrays. VHDL `&` no longer reaches
the generic bitwise-AND path: chained scalar/array concatenands are flattened,
checked against the contextual one-dimensional element profile, and retain
two-/nine-state execution domains, including arrays of records and legacy
`bit_vector`/`std_logic_vector` contexts.

Record and array assignments, conditional alternatives, case-selected
values, and supported conversions now preserve nominal base, rank, recursive
element profile, and per-dimension lengths rather than accepting unrelated
same-width packed values. The parser retains a member selected after an array
index as typed HIR; read and assignment lowering carry the array element's
record type and exact member offset through chains such as
`Pair_Value(0).Mode`. Four dedicated `FSIM-ELAB-VHCOMPOP-*` diagnostics cover
comparison, assignment, concatenation, and chained-selection failures while
the established array and overload diagnostics retain their prior contracts.

The 191-line elaboration fixture executes nested record/array equality and
inequality, matching equality/inequality, scalar/array/record-element
concatenation, unequal-length comparison, whole and selected assignment,
conditional/case values, conversion, and chained member reads/writes. Its
negative matrix covers unrelated records and arrays, conditional and length
mismatches, invalid matching domains, concatenation length, and unknown
post-index members with exact diagnostic codes. The 202-line application
differential proves interpreter and LLVM O0/O2 cold/warm equality and cache
reuse. The eight-worker 11-test Debug and Release gates passed in 4.15 and
3.98 seconds, with composite operations at 0.10/0.09 seconds, arrays at
1.92/1.87 seconds, and scoped locals at 0.84/0.80 seconds. The LLVM-disabled
ASan/UBSan 11-test gate passed in 6.93 seconds with leak detection disabled
under the managed ptrace sandbox; composite operations took 0.32 seconds and
scoped locals 0.49 seconds. The catalog covers 1,494 production diagnostics
and all 373 authored sources pass the 2,000-line gate;
`lowerer_expression.cpp` is 1,922 lines. Batch 117 remains **in progress**
with Task 8 current and is not a CI-inspection boundary.

Tasks 8 and 9 are focused-complete in the accumulated Batch 117 working tree.
VHDL read and assignment names now parse arbitrarily interleaved bounded array
indices and record selections rather than stopping after the first post-index
member. The merged array application carries an array of nested records whose
inner record contains a nominal enumeration and vector through direct,
component, generic-dependent, package/local function, and procedure
boundaries. Whole-object aliases/copies, disjoint nested member drivers,
selected sensitivity, scheduling, debugger and normalized-VCD views, and
interpreter/LLVM O0/O2 cold/warm/edit behavior agree. Adapted unconstrained
array identities now use recursive `vhdl-array-shape-v2` serialization, so
nested record, array, enumeration literal/range, nominal, offset, and shape
metadata all participate in specialization/cache identity. The same
differential compares an out-of-range runtime index followed by nested
record/array selections exactly between the interpreter and compiled engines.
One eight-worker Debug development build and the single
`fsim.application.vhdl_arrays` test passed in 2.01 seconds. Per the corrected
batch cadence, no Task 8/9 sanitizer, Release, commit, or push checkpoint was
run; Task 10 owns those accumulated gates. Batch 117 remains **in progress**
with Task 10 current and is not a CI-inspection boundary.

Task 10 is complete. Feature-matrix rows VH-239 through VH-246, the language
support and architecture contracts, the diagnostics wording, and the test
evidence inventory now record recursive nested composite HIR/layout,
enumeration context, qualification/conversion, aggregate choices, attributes,
operations, interleaved selections, boundaries, runtime/debug views, and cache
identity. The single LLVM-disabled ASan/UBSan 13-test frontend, catalog,
source, elaboration, scoped-local, record, enumeration, array, attribute,
composite-operation, and runtime gate passed in 8.16 seconds with leak
detection disabled under the managed ptrace environment. Exact LLVM 22.1.8
warnings-as-errors Debug passed all 68 tests in 176.22 seconds, with arrays at
1.88 seconds and scoped locals at 0.85 seconds. Release passed all 68 tests in
158.10 seconds, with arrays at 1.81 seconds and scoped locals at 0.84 seconds.
The diagnostic catalog covers 1,494 production codes and all 373 authored
sources pass the 2,000-line gate. A final coverage review restored the prior
multidimensional runtime-bound failure beside the new nested-chain failure;
the affected array application then passed Debug, Release, and sanitizer in
2.09, 2.04, and 1.90 seconds. All ten Batch 117 tasks are complete; Batch 117
is not a CI-inspection boundary.

### Batch 118 — VHDL access, protected, and physical types — Complete

The current ten implementation tasks are:

1. **Complete.** Audit and retain bounded access type declarations, allocators, null
   values, dereference selections, protected type declarations/bodies and
   methods, physical type ranges/units/literals, and exact source spans with
   targeted diagnostics.
2. **Complete.** Complete access designated-subtype resolution, recursive legality,
   nullable storage metadata, deterministic object identity, initialization,
   ownership, and bounded lifetime representation.
3. **Complete.** Lower supported allocators, qualified/aggregate initialization,
   dereference reads and writable targets, null checks, assignment, and
   deterministic allocation failures in the interpreter and LLVM.
4. **Complete.** Complete access equality/null operations, aliases/copies, callable
   parameters/results, copy-in/out rules, lifetime escape checks, and explicit
   diagnostics for dangling or unsupported deallocation paths.
5. **Complete.** Complete protected type/body conformance, private member layout,
   method visibility/profiles, shared-variable object construction, and
   encapsulation or purity legality.
6. **Complete.** Execute supported protected methods with deterministic mutual exclusion,
   re-entry policy, process scheduling, suspension restrictions, and
   interpreter/LLVM state parity.
7. **Complete.** Complete bounded physical type ranges, primary/secondary units,
   literal scaling, static folding, comparison/arithmetic/conversion, overflow,
   and exact time-family interoperability where legal.
8. **Complete.** Complete supported hierarchy, generic, callable, alias/copy, debugger,
   VCD, provenance, specialization, and cache behavior for access, protected,
   and physical objects without exposing host pointers in persistent identity.
9. **Complete.** Prove positive/negative parser and elaboration coverage plus interpreter,
   LLVM O0/O2, cold/warm/edit, hierarchy, callable, scheduling, debugger,
   normalized-VCD, lifetime, locking, unit-scaling, and exact-failure
   differentials.
10. **Complete.** Update matrix/diagnostics/docs and pass sanitizer, source/catalog, full
    Debug/Release, commit, and push gates before closing Batch 118.

Batch status is **complete**. All ten tasks and their gates are closed, and the
current in-progress ten-task record has moved to Batch 119 below. Tasks 1
through 9 used the corrected accumulated working-tree cadence; Task 10 owned
the single batch sanitizer, full-regression, commit, and push gate.

Task 1 adds nominal access, protected declaration/body, and physical type HIR;
retains designated subtypes, protected private state and complete method
profiles/bodies, source-ordered physical ranges and unit scales; and gives
`null`, allocator, dereference read/write, and physical-literal syntax distinct
source-spanned expression nodes. The accumulated Debug development build used
eight workers, and `fsim.frontend` passed in 0.02 seconds after a
physical-literal terminator regression was caught and repaired. No sanitizer,
Release, full regression, commit, push, or CI inspection was run at this task
boundary.

Task 2 resolves designated subtypes recursively through local, imported, and
specialized type environments and carries that resolution through generated
qualification, static folding, local nominal scoping, and canonical type
identity. Access values now have a bounded 32-bit opaque-handle contract with
zero reserved for `null`, monotonically assigned object identities capped at
4,096, owning simulation-lifetime semantics, and zero-default packed storage
for signals and persistent locals. Access cycles and protected designated
types receive access-specific elaboration failures. The eight-worker Debug
development build and focused `fsim.elaboration` test passed in 0.14 seconds;
no batch-final gates, commit, push, or CI inspection were run.

Task 3 maps each process-local nominal access type to a hidden bounded queue
whose source-level handles remain one-based packed IDs. Scalar, integer, and
packed-record allocators now support default or qualified/aggregate
initialization; whole designated values and record members can be read, and
whole dereference targets can be written. Zero handles and invalid IDs fail
through checked container indexing, while a pre-mutation capacity assertion
gives exact deterministic exhaustion. The implementation reuses existing
container SimIR, validation, interpreter, native callback, and LLVM lowering
rather than adding a host pointer or ABI surface. The eight-worker Debug build
passed focused `fsim.elaboration` and `fsim.llvm` in 0.14 and 2.50 seconds,
including interpreter execution, null failure, and a two-object exhaustion
fixture. No batch-final gates, commit, push, or CI inspection were run.

Task 4 implements nominal access equality and inequality with `null`,
same-nominal aliases and copies, access-valued function arguments/results, and
variable-class procedure `inout` copy-in/out. Access handles remain valid for
their owning process lifetime; nonnull signal escape, cross-nominal copies,
unsupported operators, and explicit `Deallocate` receive access-specific
diagnostics. A named vector subtype keeps the callable fixture within the
existing bounded callable profile. The eight-worker Debug build passed focused
`fsim.elaboration` and `fsim.llvm` in 0.15 and 2.68 seconds. No batch-final
gates, commit, push, or CI inspection were run.

Task 5 merges protected package declarations with exactly conforming bodies,
resolves bounded private member and method-profile types, and retains stable
source-ordered offsets without exposing private state as ordinary signals.
Each architecture-level `shared variable` of protected type now constructs one
stable protected-object record whose initialized private members use grouped
global container storage; nonprotected shared variables, missing or mismatched
bodies, invalid private storage, and object initializers receive protected-type
diagnostics. The conformance implementation was extracted to
`hierarchy_vhdl_protected.cpp`, leaving `hierarchy_packages.cpp` at 1,999
lines. The eight-worker Debug build passed focused `fsim.frontend` and
`fsim.elaboration` in 0.02 and 0.15 seconds. No batch-final gates, commit,
push, or CI inspection were run.

Task 6 executes supported protected procedures and direct-return functions by
loading their grouped private member snapshots, binding public formals and
private names inside an encapsulated method scope, and committing procedure
updates before the process can yield. Because protected methods are inlined as
wait-free scheduler segments, cooperative process execution supplies
deterministic mutual exclusion without a host mutex or persistent pointer.
Nested calls/re-entry, suspending bodies, unsupported profiles, unavailable
storage, ambiguous methods, and non-direct function bodies receive exact
protected-method diagnostics. A two-process test deterministically advances
the shared counter from 7 to 21; the eight-worker Debug build passed focused
`fsim.elaboration` and `fsim.llvm` in 0.16 and 3.94 seconds. No batch-final
gates, commit, push, or CI inspection were run.

Task 7 resolves bounded physical ranges to the portable signed 32-bit scalar
representation and assigns source-ordered primary-unit scale 1 plus checked,
positive secondary-unit scales expressed in earlier units. Physical literals
now fold in constants and lower contextually with exact unit, range, and
overflow failures; same-nominal addition, subtraction, scaling, division,
comparison, and explicit conversion reuse the checked VHDL integer runtime so
dynamic arithmetic overflow remains deterministic in the interpreter and
LLVM. Cross-nominal assignment or operands and unsupported operators receive
physical-type diagnostics. The positive fixture covers a statically folded
unit-valued constant, micrometer/millimeter scaling, arithmetic, division,
comparison, and conversion; negative fixtures cover unknown units, nominal
mismatch, scale overflow, and runtime arithmetic overflow. The eight-worker
Debug build and focused `fsim.elaboration` test passed in 0.15 seconds. No
batch-final gates, commit, push, or CI inspection were run.

Task 8 carries access and physical nominal metadata into public signal records,
including bounded access ownership and complete resolved physical unit/range
views, while protected shared objects retain a stable object ID and private
member container paths. Canonical type identity now includes access designated
types, physical scales/ranges, and protected layouts/method profiles; generated
qualification, static folding, dependency collection, parameter substitution,
and local nominal scoping recurse through the new type families. Tests cover
stable signal/container hierarchy paths, access and physical debug-local
records, protected member visibility, physical signal aliases and callable
returns, and two independently specialized child instances whose generic
values pass through explicit physical conversions without host pointers in
persistent identity. The eight-worker Debug build passed focused
`fsim.elaboration` in 0.16 seconds. No batch-final gates, commit, push, or CI
inspection were run.

Task 9 adds the merged `fsim.application.vhdl_advanced_types` differential.
One fixture jointly exercises access allocation/null/dereference and debug
locals, protected shared-state construction and serialized method updates, and
physical literals/arithmetic/debug locals. Interpreter, compiled LLVM O0, and
compiled LLVM O2 agree on final signals, private protected storage, completion
time/delta, debugger reads, and normalized VCD; each compiled optimization
proves cold miss/store and warm hit behavior, while an edited physical and
protected increment invalidates analysis plus specialization/native identity
and produces the expected new values. Existing focused parser/elaboration
negative cases retain exact failures for access lifetime/ownership, protected
conformance/suspension/re-entry, and physical units/range/nominal/overflow.
The eight-worker Debug focused gate passed `fsim.frontend`,
`fsim.elaboration`, `fsim.llvm`, and the new application in 3.49 seconds. No
batch-final sanitizer, Release, full regression, commit, push, or CI inspection
was run.

Task 10 is complete. The focused LLVM-disabled ASan/UBSan gate passed all 14
selected frontend, elaboration, application, runtime, catalog, and source tests
in 8.54 seconds with LeakSanitizer disabled because it cannot run under the
managed ptrace environment. The exact LLVM 22.1.8 warnings-as-errors Debug
regression passed all 69 tests in 182.47 seconds, including the advanced-types
differential in 0.59 seconds and scoped locals in 0.82 seconds. Release passed
all 69 tests in 162.84 seconds, including advanced types in 0.54 seconds and
scoped locals in 0.83 seconds. The diagnostic catalog covers 1,571 production
codes, and all 379 authored sources pass the 2,000-line gate. Static VHDL value
and physical-literal helpers now live in `elaboration_vhdl_values.cpp`, leaving
`elaboration_constants.cpp` at 1,714 lines and `elaborator_internal.hpp` at
1,998 lines. All ten Batch 118 tasks are complete; Batch 118 is not a
CI-inspection boundary.

### Batch 119 — VHDL waits, reports, files, time, and transactions — Complete

The current ten implementation tasks are:

1. **Complete.** Audit and retain nested wait forms, assertions and reports,
   file declarations and operations, TextIO profiles, physical time literals,
   and inertial, transport, and reject waveform syntax with exact source spans
   and diagnostics.
2. **Complete.** Complete wait legality and lowering in nested procedures, loops,
   conditionals, and process-local call chains, including sensitivity, timeout,
   condition, resume, and forbidden-context behavior.
3. **Complete.** Complete general assertion and report expressions, severity
   evaluation, message formatting, failure policy, source provenance, and
   interpreter/LLVM parity.
4. **Complete.** Complete bounded VHDL file types, file objects, open modes,
   status, close, lifetime, aliasing, and deterministic manifest-confined I/O.
5. **Complete.** Complete the supported `std.textio` line, read, write, endfile,
   and formatting profiles with exact cursor, whitespace, conversion, and
   failure behavior.
6. **Complete.** Complete physical `time` units, literals, conversions,
   resolution limits, static folding, arithmetic, comparison, timeout, and
   scheduling interoperability.
7. **Complete.** Complete inertial and transport waveform scheduling, reject
   limits, pulse cancellation, transaction ordering, delta behavior, and
   multi-driver resolution.
8. **Complete.** Complete supported hierarchy, callable, debugger, VCD,
   provenance, specialization, and cache behavior across waits, reports,
   files/TextIO, time, and transaction modes.
9. **Complete.** Prove positive/negative parser and elaboration coverage plus
   interpreter, LLVM O0/O2, cold/warm/edit, scheduling, debugger,
   normalized-VCD, I/O, time-resolution, and exact-failure differentials.
10. **Complete.** Update matrix/diagnostics/docs and pass sanitizer,
    source/catalog, full Debug/Release, commit, and push gates before closing
    Batch 119.

Batch status is **complete**. All ten tasks and their gates are closed, and the
current in-progress ten-task record has moved to Batch 120 below. Tasks 1
through 9 used the corrected accumulated working-tree cadence; Task 10 owned
the single batch sanitizer, full-regression, commit, and push gate. Batch 119
was not a CI-inspection boundary.

Task 1 audits and retains the complete Batch 119 frontend surface. Nominal
`file of` types now preserve their element subtype, and file objects in
package, architecture, process, and block regions preserve source spans,
open-kind expressions, logical-name expressions, and file-interface object
classes. Ordinary `file_open`, `file_close`, `readline`, `writeline`,
`read`/`write`, and `endfile` calls remain exact selected-name/call HIR.
Assertions and reports retain general report and severity expressions while
literal text and predefined severities continue to mirror the legacy compact
metadata. Wait, rejection-limit, and waveform delays retain general physical-
time expressions while integer/unit literals preserve the established compact
form. The stale parser-only nested-wait rejection was removed after confirming
that the complete statement tree was already retained; the independent
process-sensitivity-list legality diagnostic remains. The new positive and
malformed frontend fixture covers nested waits, TextIO-like profiles/calls,
file declarations/open information, physical literals and expressions,
inertial/transport/reject waveforms, exact spans, and targeted diagnostics.
Eight-worker warnings-as-errors Debug builds of the frontend and elaboration
targets succeeded; focused frontend, elaboration, catalog, and source gates
passed in 0.40 seconds. The catalog covers 1,577 production codes, and all 380
authored sources pass the 2,000-line gate. No sanitizer, Release, full
regression, commit, push, or CI inspection was run at this task boundary.

Task 2 completes nested wait execution and legality. VHDL procedures may now
retain supported waits, and the ordinary SimIR `Call` frame remains live while
the process suspends inside a procedure. Lowering records exact overload-
resolved process-to-procedure, procedure-to-procedure, and function-to-
procedure dependencies, then chooses implicit process repetition only after
all reachable callable bodies have been lowered. This admits waits nested
through conditionals, static loops, and multi-level procedure chains without
misclassifying a same-name nonsuspending overload. Sensitized processes and
functions diagnose direct or transitive suspension. A focused elaboration
fixture proves event, condition, absolute timeout, resume, overload, and
forbidden-context behavior in the interpreter; a dedicated application
differential proves the same procedure-chain schedule in the interpreter and
LLVM O0/O2. Eight-worker Debug builds and focused `fsim.elaboration` and
`fsim.application.vhdl_procedure_waits` tests pass. The catalog covers 1,578
production codes, and all 382 authored sources pass
the 2,000-line gate with `elaborator_internal.hpp` exactly at the limit. No
sanitizer, Release, full regression, commit, push, or CI inspection was run at
this task boundary.

Task 3 completes general VHDL assertion and report execution. The frontend
recognizes `string` and `severity_level`, decodes doubled-quote string
literals, and retains runtime string concatenation. Lowering evaluates report
and severity expressions at the statement execution point, but branches over
both for a passing assertion. Static forms keep the compact `Assert`/`Report`
operations; dynamic forms use typed string and two-bit severity registers in
a `StringReport` operation. Interpreter and LLVM paths validate the runtime
severity ordinal, preserve exact source metadata, continue after note through
error, report a standalone failure exactly once before terminating, and avoid
double-reporting a failed assertion. `FSIM-ELAB-VHREPORT-001` and `-002`
diagnose non-string report expressions and non-`severity_level` severities.
The elaboration fixture covers dynamic messages, severities, skipped passing
assertions, and both diagnostics. The merged display application proves exact
messages, severities, source lines, failure policy, and interpreter/LLVM O0/O2
parity. Eight-worker warnings-as-errors Debug builds succeeded; focused
frontend, elaboration, application, catalog, and source gates passed in 0.59
seconds. The catalog covers 1,580 production codes, and all 382 authored
sources pass the 2,000-line gate. No sanitizer, Release, full regression,
commit, push, or CI inspection was run at this task boundary.

Task 4 completes bounded executable VHDL file objects in process, procedure,
and nested block regions. Nominal file objects use opaque 32-bit service
handles rather than host descriptors, remain confined to the manifest root,
and support declaration opens plus status and nonstatus `file_open`, static
`read_mode`/`write_mode`/`append_mode`, `file_close`, lookahead-preserving
`endfile`, and direct signed-integer element `read`/`write`. Status-form opens
return `open_ok`, `status_error`, `name_error`, or `mode_error` without
terminating the process; reopening an open object preserves its handle.
Procedure file formals alias state through copy-in/out, lexical fallthrough
closes block-owned objects, and a common procedure epilogue closes every owned
file even after an early nested return. Direct reads require one complete
conversion and fail deterministically otherwise. `FSIM-ELAB-VHFILE-001`
through `-012` reject invalid declarations, modes, objects, profiles, element
types, and targets. The merged file application proves status, close/alias,
lookahead, input/output bytes, and interpreter/LLVM O0/O2 parity. An
eight-worker warnings-as-errors Debug build succeeded; focused diagnostics,
source, elaboration, file-application, and runtime gates passed in 1.15
seconds. The catalog covers 1,592 production codes, and all 382 authored
sources pass the 2,000-line gate. No sanitizer, Release, full regression,
commit, push, or CI inspection was run at this task boundary.

Task 5 completes the bounded `std.textio` execution profile. Built-in `line`
objects use the existing 4,096-byte string-register plane and `side` retains
the `right`/`left` ordinals. `readline` strips LF or CRLF and fails at true
EOF; `writeline` appends one newline and clears the line. Integer, Boolean,
and bit `read` skip leading whitespace, consume exactly the parsed cursor
prefix, update an optional Boolean `good`, preserve the value on conversion
failure, and otherwise fail deterministically. Integer, Boolean, bit, and
string `write` append to the line with static `left`/`right` justification and
a bounded static field width. All paths reuse `FileReadLine`, `FileScan`,
`StringMethod`, and `FileWriteString`; scan cursor, success, line-clear, and
TextIO mode metadata participate in native schema 73. Nine exact
`FSIM-ELAB-VHTEXTIO-*` diagnostics bound unsupported profiles. The merged
file application proves whitespace/cursor behavior, success and failure,
formatting, line clearing, exact bytes, and interpreter/LLVM O0/O2 parity.
An eight-worker warnings-as-errors Debug build succeeded; focused catalog,
source, elaboration, application, and runtime gates passed in 1.29 seconds.
The catalog covers 1,601 production codes, and all 384 authored sources pass
the 2,000-line gate. No sanitizer, Release, full regression, commit, push, or
CI inspection was run at this task boundary.

Task 6 completes bounded predefined VHDL physical time. `time` is a
nonnegative signed-64-bit tick type, and standard `fs`, `ps`, `ns`, `us`,
`ms`, `sec`, `min`, and `hr` literals are recursively normalized before
elaboration. Exact rational cancellation handles coarse resolutions without
intermediate femtosecond overflow; qualifications, declaration-ordered static
arithmetic/comparison, expression waits, and procedure timeouts therefore
share one integer SimIR representation. Expression-valued units contribute to
automatic resolution selection. `FSIM-ELAB-VHTIME-001` through `-003`
separate nonstatic, final-representation overflow, and inexact-resolution
failures. The focused application proves all units, conversion, arithmetic,
comparison, timeout, automatic resolution, exact failures, scheduling, and
interpreter/LLVM O0/O2 parity. Native schema 74 isolates the changed time
semantics. Eight-worker warnings-as-errors Debug builds succeeded; focused
frontend, catalog, source, elaboration, and application gates passed in 0.46
seconds. The catalog covers 1,604 production codes, and all 384 authored
sources pass the 2,000-line gate. No sanitizer, Release, full regression,
commit, push, or CI inspection was run at this task boundary.

Task 7 completes bounded VHDL projected-output transaction semantics. Whole,
static-slice, and runtime-slice assignments atomically replace each
process-owned scalar driver's ordered future transaction list. Transport
truncates at the first new timestamp; inertial mode applies the LRM marking
algorithm with either the explicit static rejection expression or the first
waveform delay. Cancellation is independent per scalar, same-time updates
coalesce deterministically, zero-delay cascades advance exact delta cycles,
and resolved `std_logic` drivers publish one effective value after every
update phase. The focused projected-waveform application now combines
implicit and explicit pulse rejection, transport preservation, ordered
multi-element whole/slice/conditional/selected waveforms, expression-valued
rejection and delay, a two-driver `0`/`1`/`Z` resolution sequence, a two-delta
zero-time cascade, exact VCD, cold/warm native cache reuse, and
interpreter/LLVM O0/O2 parity. Eight-worker warnings-as-errors Debug builds
succeeded; focused frontend, catalog, source, elaboration, application, and
runtime gates passed in 0.60 seconds. The catalog remains at 1,604 production
codes, and all 384 authored sources pass the 2,000-line gate. No sanitizer,
Release, full regression, commit, push, or CI inspection was run at this task
boundary.

Task 8 closes the supported cross-feature metadata and specialization paths.
A time-generic VHDL child now accepts an exact 64-bit physical-time actual and
suspends through its nested procedure chain while driving parent-visible
ports. The hierarchy differential compares interpreter, compiled O0/O2, and
forced-O0 debug execution points including source, lexical child scope,
process/instruction identity, final values, scheduler changes, and normalized
VCD. Its two specialization modules prove cold stores and warm hits without
changing canonical specialization keys. VHDL report runs now prove cold/warm
cache identity while retaining exact severity/source metadata; VHDL file and
TextIO runs do the same while retaining source-bearing execution points and
deterministic bytes. The projected-waveform differential already covers
VCD, cold/warm cache reuse, time normalization, and resolved transactions.
Eight-worker warnings-as-errors Debug builds succeeded; the focused
frontend, catalog, source, elaboration, report, file/TextIO, wait/time,
projected-waveform, and runtime gates passed in 2.05 seconds. The catalog
remains at 1,604 production codes, and all 384 authored sources pass the
2,000-line gate. No sanitizer, Release, full regression, commit, push, or CI
inspection was run at this task boundary.

Task 9 closes the accumulated Batch 119 differential matrix. The frontend and
elaboration suites retain positive and malformed waits, reports, file/TextIO,
physical-time, and waveform cases with exact diagnostics. Runtime applications
now compare interpreter, LLVM O0/O2, and forced-O0 debug results across nested
callable suspension, source/severity reports, manifest-confined I/O, standard
time units and resolution failures, projected scheduling, delta cycles,
resolved drivers, and normalized VCD. Cold/warm runs cover every native path;
the wait/time hierarchy also edits its VHDL source, changes both canonical
specialization keys, misses and stores both native modules, and proves the new
six-tick schedule. TextIO conversion failures execute in both engines, while
file-input and source edits remain independently distinguished. The focused
nine-test accumulated gate passed in 1.83 seconds with eight-worker builds,
1,604 cataloged production diagnostics, and all 384 authored sources under the
2,000-line limit. No sanitizer, Release, full regression, commit, push, or CI
inspection was run at this task boundary.

Task 10 is complete. Feature-matrix rows `V1-VH-06` and `V1-VH-07` now carry
executable evidence for nested waits, reports, files/TextIO, physical time,
and projected inertial/transport/reject transactions. The LLVM-disabled
ASan/UBSan suite passed all 67 tests in 340.81 seconds outside the ptrace-
restricted sandbox. Exact LLVM 22.1.8 warnings-as-errors Debug passed all 70
tests in 188.12 seconds, and Release passed all 70 tests in 164.46 seconds.
`fsim.application.scoped_locals` remained quick at 0.58 seconds under the
sanitizers and 0.87 seconds in both Debug and Release. The diagnostic catalog
covers 1,604 production codes, and all 384 authored sources pass the
2,000-line gate. The single Batch 119 commit and push close the accumulated
gate; no CI inspection is required at this non-tenth-batch boundary.

### Batch 120 — reviewed Apache-2.0 IEEE packages and VHDL v1 audit — Complete

The current ten implementation tasks are:

1. **Complete.** Audit the existing standard-library loader, bundled-source
   inventory, licenses, package dependencies, VHDL revisions, and current IEEE
   package coverage before selecting the reviewed Apache-2.0 source bundle.
2. **Complete.** Review, bundle, analyze, and execute the supported
   `ieee.std_logic_1164` declarations, bodies, tables, conversions, resolution,
   edges, and vector operations with retained license and provenance.
3. **Complete.** Review, bundle, analyze, and execute the supported
   `ieee.numeric_std` and `ieee.numeric_bit` signed, unsigned, conversion,
   resize, comparison, arithmetic, shift, rotate, and boundary profiles.
4. **Complete.** Complete the bundled bit and logic utility package profiles,
   including vector/string conversions, matching values, edge behavior,
   overload visibility, and exact unsupported-profile diagnostics.
5. **Complete.** Review, bundle, analyze, and execute bounded
   `ieee.fixed_generic_pkg` and `ieee.fixed_pkg` types, generics, conversions,
   resize, rounding, overflow, arithmetic, comparison, and slice behavior.
6. **Complete.** Review, bundle, analyze, and execute bounded
   `ieee.float_generic_pkg` and `ieee.float_pkg` types, generics, conversions,
   classification, rounding, arithmetic, comparison, and exceptional values.
7. **Complete.** Complete dependency-ordered implicit/explicit library,
   context, `use`, package-body, overload, generic-package, and type-identity
   integration for every bundled package without host-install dependencies.
8. **Complete.** Complete hierarchy, callable, debugger, VCD, provenance,
   specialization, cold/warm/edit cache, interpreter, and LLVM O0/O2 behavior
   for designs consuming the reviewed packages.
9. **Complete.** Audit every required VHDL v1 feature-matrix row and prove the
   accumulated positive, negative, elaboration, runtime, portability, license,
   and package-conformance differential matrix.
10. **Complete.** Update matrix/diagnostics/docs, pass sanitizer,
    source/catalog, full Debug/Release, commit, and push gates, then inspect and
    repair every non-documentation GitHub CI failure at the mandatory Batch 120
    boundary.

Batch status is **complete**. This exact ten-task list remains the retained
record in both the official plan and this handoff. Tasks 1 through 9 use
the corrected accumulated working-tree cadence; Task 10 owns the single batch
sanitizer, full-regression, commit, push, and mandatory non-documentation CI
inspection gate. Local builds use at least eight workers; GitHub Actions builds
use parallelism four.

Task 1 selects and retains the official IEEE-P1076 `1076-2019` package tag at
commit `16a012320947d378611cc7457f64ed76cb52bac4`. The upstream `ieee` and
`std` VHDL directories, Apache-2.0 license, and authorship file are bundled
byte-for-byte; checked SHA-256 values cover all 28 imported files. A separate
fsim-authored inventory records the only supported dependency order and review
stage for the predefined, TextIO, environment, reflection, logic, numeric,
math, fixed, and floating packages. CMake installs the complete reviewed-source
snapshot, while the application activates only stages with executable evidence
and a corresponding standard-library cache version. An eight-worker exact LLVM
22.1.8 Debug regeneration succeeded;
the new integrity/license test plus the diagnostic-catalog and source-line
gates passed all three tests in 0.23 seconds. The catalog remains at 1,604
production codes, and all 384 authored C/C++ sources remain within the
2,000-line limit. No sanitizer, Release, full regression, commit, push, or CI
inspection was run at this task boundary.

Task 2 activates the checksum-pinned `ieee.std_logic_1164` declaration and
body only for VHDL contexts that explicitly consume that package. The exact
upstream bytes remain separate compiler-supplied checked sources; their paths,
contents, compilation-unit digests, and semantic dependency identity enter the
design and specialization cache keys without changing project-manifest source
counts or ordering. A compact intrinsic projection preserves the existing
nine-state runtime type identity while recording the reviewed upstream
revision and declaration inventory. The focused application proves all nine
input states through NOT/AND/OR/XOR and derived NAND/NOR/XNOR vector tables,
two-driver standard resolution, `rising_edge`/`falling_edge`, bounded
same-domain `std_logic_vector`/`std_ulogic_vector` conversions, projected
transactions, exact locals, VCD, cold/warm native reuse, and interpreter versus
LLVM O0/O2 parity. It also proves the pinned declaration/body SHA-256 values
and rejects a project redeclaration with `FSIM-FE-VHSTD-004`. An eight-worker
Debug build succeeded; frontend, elaboration, runtime, package-integrity,
diagnostic-catalog, source-line, and focused application tests passed all seven
tests in 0.80 seconds, with the application itself at 0.33 seconds. Task 3 is
now current; the batch worktree remains intentionally uncommitted and no
sanitizer, Release, full regression, push, or CI inspection was run.

Task 3 activates the exact reviewed `numeric_std` or `numeric_bit` declaration
and body on explicit use, with `numeric_std` also bringing its pinned
`std_logic_1164` dependency into deterministic analysis and cache order. The
parser distinguishes two-state `numeric_bit` signed/unsigned objects from the
nine-state `numeric_std` profiles. Bounded intrinsic lowering now executes
`to_integer`, `to_signed`, `to_unsigned`, `resize`, `shift_left`,
`shift_right`, `rotate_left`, and `rotate_right`; direct signed/unsigned
conversion preserves bits while changing arithmetic interpretation. The
focused application covers add, subtract, multiply, divide, modulo, absolute
value, comparison, sign extension, truncation/conversion, shift/rotate,
integer conversion, exact package hashes and declaration metadata, O0/O2
interpreter/LLVM parity, and cold/warm native reuse for both packages. Invalid
result sizes and out-of-profile integer-conversion widths produce
`FSIM-ELAB-VHNUM-002` and `FSIM-ELAB-VHNUM-003`. After returning the shared
elaborator header from 2,004 to exactly 2,000 lines, the eight-worker Debug
build and all nine focused frontend, elaboration, runtime, integer-shift,
logic9, numeric, package-integrity, catalog, and source-budget tests passed in
1.44 seconds; the numeric application took 0.52 seconds. Task 4 is current;
the accumulated batch remains uncommitted, and no sanitizer, Release, full
regression, push, or CI inspection was run.

Task 4 completes the bounded bit/logic utilities without adding another
backend operation family. Scalar and vector conversions lower into existing
extract, exact-compare, conditional-select, concatenate, and typed-copy SimIR,
covering `to_bit`, `to_bitvector`, bit-to-logic promotion, `to_01`, `to_x01`,
`to_x01z`, `to_ux01`, and `is_x` across all nine states. Static one- through
64-bit `to_string`, `to_ostring`, and `to_hstring` profiles produce exact
binary/octal/hex strings; dynamic or unknown octal/hex profiles fail with
`FSIM-ELAB-VHLOGIC-003`. The checksum-pinned `std_logic_textio` declaration is
now loaded after `std_logic_1164` and retains its reviewed alias inventory.
The expanded logic application proves scalar/vector overloads, xmap behavior,
matching-known/unknown predicates, edges, exact reports, interpreter/LLVM
O0/O2, VCD, and cold/warm cache behavior. The eight focused frontend,
elaboration, runtime, logic, numeric, package-integrity, catalog, and source
tests passed in 2.17 seconds; the logic application took 1.20 seconds. Task 5
is current, with no batch commit, sanitizer, Release, full regression, push,
or CI inspection yet.

Task 5 activates the checksum-pinned `math_real`, `fixed_float_types`,
`fixed_generic_pkg`, and `fixed_pkg` dependency chain while retaining the
reviewed default fixed-package rounding and overflow profiles. Constrained
`ufixed` and `sfixed` objects carry their descending binary-point ranges over
the common exact nine-state packed representation. Bounded lowering executes
locally static integer `to_ufixed`/`to_sfixed` conversions with saturation,
same-range add/subtract and comparison through the shared signed/unsigned
arithmetic kernels, unsigned fractional `resize` with nearest rounding,
scale-preserving resize, and fixed-point slices. The focused application
proves all ten exact compiler-supplied source dependencies, declaration
inventory/revision, O0/O2 interpreter/LLVM parity, cold/warm native reuse,
positive and negative conversion, rounding, saturation, arithmetic,
comparison, and slicing. Ascending contextual ranges and widths above 64
produce `FSIM-ELAB-VHFIX-004` and `FSIM-ELAB-VHFIX-002`. The eight-worker
Debug builds succeeded and all nine focused frontend, elaboration, runtime,
logic, numeric, fixed, package-integrity, catalog, and source-budget tests
passed in 2.53 seconds; the fixed application took 0.33 seconds. Task 6 is
current; the accumulated batch remains intentionally uncommitted, and no
sanitizer, Release, full regression, push, or CI inspection was run.

Task 6 activates the checksum-pinned `float_generic_pkg` declaration/body and
`float_pkg` instance after their complete 13-source logic, numeric, math,
fixed, and floating dependency chain. The bounded default generic profile is
IEEE-754 binary32: constrained `float(8 downto -23)` values retain their exact
32 nine-state bits, while locally static package calls fold before SimIR into
ordinary typed constants. Integer conversion, default binary32 rounding,
`add`, `subtract`, `multiply`, `divide`, `sqrt`, named comparisons,
`to_integer`, and raw standard-logic-vector conversion are covered alongside
finite, NaN, unordered, and sign classification. Canonical positive/negative
zero, infinity, signaling/quiet NaN constructors preserve exact bits.
Nonstatic operations, non-binary32 ranges, and exceptional integer conversion
produce `FSIM-ELAB-VHFLT-001`, `FSIM-ELAB-VHFLT-002`, and
`FSIM-ELAB-VHFLT-004`. The eight-worker Debug builds succeeded and all ten
focused frontend, elaboration, runtime, logic, numeric, fixed, float,
package-integrity, catalog, and source-budget tests passed in 2.95 seconds;
the float application took 0.26 seconds. Task 7 is current; no sanitizer,
Release, full regression,
commit, push, or CI inspection was run.

Task 7 completes the dependency/visibility integration boundary for all ten
activated package declarations and their six bodies. One reusable project
context imports `std_logic_1164`, `std_logic_textio`, `numeric_bit`,
`numeric_std`, `fixed_pkg`, and `float_pkg`; their implicit math/fixed/generic
dependencies expand into 16 exact compiler-supplied sources before the three
manifest units without changing manifest counts or order. The integration
fixture proves declaration-before-body and complete package dependency order,
context-reference visibility, default generic-package instances, independent
two-state `ieee.numeric_bit.unsigned` and nine-state
`ieee.numeric_std.unsigned` identity, and simultaneous overload dispatch for
logic mapping, numeric conversion/resize, fixed resize, and floating
arithmetic. Fully qualified reviewed intrinsic declarations now count as
package exports in package-reference specialization, retain source closure,
and pass unchanged to their intrinsic lowerers; unknown ordinary exports keep
the existing `FSIM-ELAB-PKG-010` path. After compressing the qualified-export
change from 2,003 to 1,999 lines, the eight-worker Debug build and all 11
focused frontend, elaboration, runtime, per-package, integration,
package-integrity, catalog, and source-budget tests passed in 3.54 seconds;
the integration application took 0.52 seconds. Task 8 is current; no
sanitizer, Release, full regression, commit, push, or CI inspection was run.

Task 8 extends the all-package context fixture through the complete consumer
execution boundary. The top specialization retains the exact numeric-bit,
numeric-standard, fixed, and floating package source closure; stable hierarchy
paths expose every result signal. A waiting VHDL process retains independently
typed two-state numeric, nine-state numeric, fixed, and binary32 debugger
locals with exact values. The interpreter and LLVM O0/O2 runs produce the
same signal values, locals, and normalized VCD, while cold/warm cache telemetry
proves module stores and hits. A comment-only edit to the reusable context
invalidates every affected O2 native module and preserves behavior. The
eight-worker Debug builds and all 11 focused frontend, elaboration, runtime,
per-package, integration, package-integrity, catalog, and source-budget tests
passed in 3.43 seconds; the expanded integration application took 0.69
seconds. Task 9 is current; the batch remains intentionally uncommitted,
with no sanitizer, Release, full regression, push, or CI inspection yet.

Task 9 closes the VHDL release-authority audit. Aggregate rows `V1-VH-01`
through `V1-VH-05` now state the bounded contracts actually completed by
Batches 111–118 and join `V1-VH-06` through `V1-VH-08` at executable status;
every row carries positive, negative, elaboration, and runtime evidence. The
new `fsim.v1-vhdl-matrix` gate requires exactly eight ordered executable rows
with no empty evidence column. After building the previously untouched third
application shard with eight workers, the complete Debug VHDL-labeled suite
passed 28/28 tests in 12.99 seconds across analysis order, contexts, overloads,
all generic kinds, components/configurations, statements, composites,
advanced types, transactions, reviewed packages, interpreter/LLVM, hierarchy,
debugger, VCD, and cache behavior.

Task 10's local release gates pass. The LLVM-disabled ASan/UBSan regression
passed all 73 tests in 340.64 seconds with LeakSanitizer disabled for the
managed ptrace environment; `fsim.application.scoped_locals` took 0.51
seconds. Exact LLVM 22.1.8 warnings-as-errors Debug passed all 76 tests in
186.74 seconds, and Release passed all 76 in 161.14 seconds; scoped locals
took 0.82 seconds in each. The reviewed IEEE integration application took
0.73 seconds in Debug and 0.62 seconds in Release. The diagnostic catalog
covers 1,623 production codes, all 393 authored sources pass the 2,000-line
gate, and the IEEE inventory and exact eight-row VHDL v1 matrix gates pass.
This record is the single accumulated commit/push checkpoint. Initial CI run
`30763877162` exposed an MSVC oversized string literal and a clang-cl deleted
defaulted comparison warning. Repair run `30764329695` exposed one signed/
unsigned comparison plus Windows newline conversion of checksum-pinned IEEE
sources. Repair run `30765075997` then exposed one CRLF-sensitive generated-
source edit locator. Commits `deb27c4`, `15ac189`, and `4ad6153` repair those
failures while preserving exact IEEE bytes and newline-neutral source edits.
Final non-documentation run `30765734570` passed all 12 jobs. Standard MSVC
Debug passed 75/75 tests in 372.25 seconds with scoped locals in 0.27 seconds;
MSVC plus LLVM Debug passed 76/76 in 1,160.90 seconds with scoped locals in
1.77 seconds. Batch 120 and its mandatory CI boundary are complete.

### Batch 121 — mixed-language value-boundary conversions — Complete

The current ten implementation tasks are:

1. **Complete.** Audit existing VHDL/SystemVerilog/SystemC boundary type
   metadata, shared-signal aliases, direction rules, diagnostics, and ML-005/
   ML-006 evidence; define the bounded conversion and failure matrix.
2. **Complete.** Implement equal-count ordinal vector mapping across differing
   ascending/descending VHDL and SystemVerilog packed ranges in both hierarchy
   directions, with stable conversion ownership and source metadata.
3. **Complete.** Implement bounded input/output width adaptation with explicit
   truncation, zero extension, sign extension, and inout/lossy-width rejection
   rules instead of requiring every boundary width to be identical.
4. **Complete.** Implement signed/unsigned integral boundary adaptation after
   each language's width rules, including direction-aware legality and exact
   diagnostics for unsafe aliases.
5. **Complete.** Implement VHDL Boolean to/from one-bit SystemVerilog bit/logic
   conversions with canonical false/true ordinals and checked noncanonical
   incoming values.
6. **Complete.** Complete VHDL integer-family to/from 32-bit signed
   SystemVerilog integral conversion, subtype range checks, and both hierarchy
   directions without conflating integer and packed-vector identity.
7. **Complete.** Complete two-state VHDL bit/bit_vector and SystemVerilog bit
   scalar/vector boundaries, including ordinal range conversion and explicit
   rejection of state-losing reverse flows.
8. **Complete.** Complete four-state SystemVerilog logic and nine-state VHDL
   std_logic/std_ulogic scalar/vector conversion tables, exact legal collapse,
   unknown/high-impedance handling, and lossy-domain diagnostics.
9. **Complete.** Prove the complete conversion matrix through recursive mixed
   hierarchy, interpreter, LLVM O0/O2, cold/warm/edit cache, debugger, VCD,
   provenance, and positive/negative elaboration evidence; close ML-005 and
   ML-006 in the feature matrix.
10. **Complete.** Update matrix/diagnostics/docs, pass sanitizer, source/catalog,
    and full Debug/Release gates, then create and push the single Batch 121
    checkpoint. This is not a mandatory CI-inspection boundary.

Batch status is **complete**. Keep this exact ten-task
list current in both the official plan and this handoff. Tasks 1 through 9 use
one accumulated dirty worktree with focused eight-worker Debug builds and
tests; Task 10 owns the sanitizer, full regressions, documentation, commit, and
push gate. GitHub builds use parallelism four, and Batch 121 does not require a
non-documentation CI inspection.

Task 1 confirms that every ordinary boundary reaches one shared validator and
that `SignalInfo` already retains width, source domain, signedness, packed
range/direction, integer subtype range, nominal identity, aggregate shape, and
declaration metadata. HDL and SystemC connections currently bind the formal
and actual names directly to one scheduler signal ID after validation. The
validator rejects unknown domains, cross-language aggregates/arrays/
enumerations, unequal widths, multi-bit signedness differences, unsafe integer
subtype aliases, state-losing flows into two-state destinations, and unresolved
cross-language inouts. Existing common-domain selection and language-local
reads provide equal-width bidirectional Logic9/four-state collapse, while the
integer fixture proves exact signed 32-bit aliases and range checks. The
remaining ML-005/ML-006 gaps are structural: no boundary conversion object or
process owns provenance/cache identity, cross-language packed bounds and
directions do not produce ordinal remapping, width and signedness adaptation is
rejected rather than executed, and Boolean/integer/state conversions lack one
complete atomic matrix. Exact LLVM Debug elaboration, the mixed application,
and the Logic9 application passed 3/3 focused tests in 17.36 seconds (0.17,
15.98, and 1.22 seconds). Task 2 is current; the Batch 121 worktree is now the
intentional accumulated dirty checkpoint, with no sanitizer, Release, full
regression, commit, push, or CI inspection at this task boundary.

Task 2 makes equal-count ordinal vector boundaries explicit without adding a
redundant runtime copy. Both frontends already normalize the leftmost declared
packed element to the most-significant canonical ordinal, so opposite numeric
bounds and directions can safely share one scheduler signal. The new
`BoundaryConversionInfo` DesignIR record is emitted only after successful
cross-language packed validation and retains the owning port path, canonical
signal ID, direction, formal/actual domains and signedness, both declared
ranges, connection span, formal declaration span, and actual declaration span.
Bidirectional VHDL/SystemVerilog fixtures use explicit per-index reads/writes
to prove `"10XZ"` across `[1:4]` to `7 downto 4`, `[9:6]` to `20 to 23`, and
the reverse hierarchy direction while checking stable metadata and physical
source identities. The exact LLVM Debug elaboration target built with eight
workers; diagnostics catalog, source-line budget, and elaboration passed 3/3
tests in 0.34 seconds. The internal elaborator header remains exactly 2,000
lines. Task 3 is current; no sanitizer, Release, full regression, commit, push,
or CI inspection ran at this task boundary.

Task 3 implements one- through 64-bit width-changing input, output, and buffer
boundaries while retaining targeted rejection of unequal-width inouts. A
width-changing connection owns a separate formal signal and one deterministic
adapter process in the parent specialization. The process reads the source on
initialization and any-change sensitivity, truncates least-significant
ordinals with `Extract`, zero-extends unsigned values or replicates the dynamic
sign ordinal before `Concatenate`, writes in the common update phase, and
retains an exact whole-signal driver region. Its process ID, formal/actual
signal IDs, widths, ranges, domains, signedness, path, and source spans are
retained in `BoundaryConversionInfo`, so ordinary process/cache identity sees
the conversion rather than hiding it in an alias. Five adapters in each
VHDL-parent/SV-child and SV-parent/VHDL-child direction prove input zero/sign
extension and truncation plus output zero extension and truncation; the
observed values are `00001010`, `11111010`, `0110`, `00001010`, and `0110`.
An unequal-width resolved inout remains rejected by `FSIM-ELAB-BIND-020`.
Eight-worker LLVM Debug builds succeeded; diagnostics catalog, source budget,
and elaboration passed 3/3 in 0.35 seconds. Authored files remain within 2,000
lines. Task 4 is current; no sanitizer, Release, full regression, commit, push,
or CI inspection ran at this task boundary.

Task 4 permits explicit signed/unsigned adaptation for one- through 64-bit
cross-language input, output, and buffer ports while retaining the same-
language and inout safety checks. Equal-width signedness changes receive a
bit-preserving `CopyRegister` adapter rather than an unsafe shared type alias.
When width also changes, truncation remains ordinal and widening follows the
source side's signedness: unsigned sources zero-extend and signed sources
replicate their dynamic most-significant ordinal before the destination type
view is applied. DesignIR distinguishes `signedness_adapter` from
`width_signedness_adapter`, with both signal identities and the adapter process
retained. Each VHDL-parent/SV-child and SV-parent/VHDL-child fixture proves two
signedness-only and two combined adapters: unsigned `1010` widens into a signed
destination as `00001010`, a signed `1010` source widens into an unsigned
destination as `11111010`, and equal-width conversions preserve `1010`.
Resolved inout width and signedness mismatches remain targeted
`FSIM-ELAB-BIND-020`/`FSIM-ELAB-BIND-021` failures. The exact eight-worker LLVM
Debug build succeeded; diagnostics catalog, source-line budget, and elaboration
passed 3/3 in 0.34 seconds, with the largest touched test at 1,730 lines. Task
5 is current; no sanitizer, Release, full regression, commit, push, or CI
inspection ran at this task boundary.

Task 5 gives every one-bit VHDL Boolean/SystemVerilog `bit` or `logic`
boundary its own `boolean_adapter` process and formal signal instead of
conflating the nominal Boolean with a packed alias. Boolean-to-SystemVerilog
copies preserve the canonical false/true ordinals. SystemVerilog-to-Boolean
adapters widen the incoming scalar to the existing checked 32-bit integer
representation and require the exact range zero through one before committing
the original bit; four-state `X`/`Z` therefore raises the existing unknown or
high-impedance runtime failure. A Logic4-to-Boolean checker arms its sensitivity
before the first read so an undriven time-zero `logic` default is not mistaken
for a driven noncanonical value. VHDL-parent/SV-child and SV-parent/VHDL-child
fixtures each prove Boolean-to/from both `logic` and `bit`; a post-start `X`
transition proves checked rejection. Scalar conversion metadata retains both
domains, signals, process identity, direction, and source spans with absent
packed ranges. The conversion tests now have a separate 235-line translation
unit, preserving the existing 1,730-line mixed test. The exact eight-worker
LLVM Debug build succeeded; diagnostics catalog, source-line budget, and
elaboration passed 3/3 in 0.36 seconds. Task 6 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this task
boundary.

Task 6 gives each exact 32-bit signed VHDL integer-family/SystemVerilog `bit`
or `logic` boundary a distinct `integer_adapter` process and formal signal, so
integer nominal identity is no longer conflated with packed-vector identity.
The destination VHDL subtype's bounds are retained in DesignIR and enforced by
`IntegerCheck` before the original 32-bit value is committed; four-state
unknown or high-impedance operands fail the same checked path. Logic4 sources
arm sensitivity before their first read to avoid inspecting an undriven
time-zero default. Both VHDL-parent/SV-child and SV-parent/VHDL-child fixtures
prove signed `bit` and `logic` flow in both directions, distinct signal
ownership, negative values, destination range metadata, a deliberate
out-of-range transition, and an all-`X` transition. Unsigned SystemVerilog
profiles are rejected by `FSIM-ELAB-BIND-021` and the generalized range-safe
conversion diagnostic `FSIM-ELAB-BIND-051`; same-language integer subtype
aliases retain their direction-aware containment checks. The exact
eight-worker LLVM Debug build succeeded; diagnostics catalog, source-line
budget, and elaboration passed 3/3 in 0.38 seconds. The new conversion test is
458 lines and the largest touched test remains 1,730 lines. Task 7 is current;
no sanitizer, Release, full regression, commit, push, or CI inspection ran at
this task boundary.

Task 7 completes equal-width VHDL `bit`/`bit_vector` and SystemVerilog `bit`
boundaries as canonical two-state ordinal aliases. Vector ports retain both
declared ranges and directions, while scalar ports now also emit an explicit
`ordinal_alias` DesignIR record with absent packed ranges; both forms retain
one scheduler signal and require no adapter process. VHDL-parent/SV-child and
SV-parent/VHDL-child fixtures each prove vector transfer across opposing and
differently numbered ranges plus scalar transfer, exact Bit2 domains, alias
identity, and `1010`/`0101`/`1` runtime values. Separate negative fixtures
prove both output-directed and input-directed Logic4-to-Bit2 state loss remains
an elaboration-time `FSIM-ELAB-BIND-022` failure. The exact eight-worker LLVM
Debug build succeeded; diagnostics catalog, source-line budget, and
elaboration passed 3/3 in 0.36 seconds. The accumulated conversion test is 729
lines and `hierarchy_types.cpp` is 1,627 lines. Task 8 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this task
boundary.

Task 8 makes equal-width Logic4/Logic9 coercion explicit in DesignIR as a
`state_domain_alias` while retaining the canonical shared scheduler signal.
The owning signal's value kind and each language-local register kind already
perform the conversion at every read and write, so no redundant process is
needed; width/signedness adapters retain their structural kind and now carry a
separate `state_domain_changed` flag. The locked collapse table is
`U/X/W/- -> X`, `0/L -> 0`, `1/H -> 1`, and `Z -> Z`; reverse expansion is
`0/1/X/Z -> 0/1/X/Z`. VHDL-parent/SV-child evidence drives every Logic9 value
through `std_logic_vector`, `std_ulogic_vector`, `std_ulogic`, and `std_logic`
ports and observes `XX01ZX01X` plus `1`. SV-parent/VHDL-child evidence expands
and returns `01XZ` plus scalar `Z`. Both hierarchy directions retain exact
domains, alias identity, source metadata, and optional scalar/vector ranges.
Logic9-to-SystemVerilog-`bit` state loss remains a targeted
`FSIM-ELAB-BIND-022` elaboration failure, complementing Task 7's Logic4-to-Bit2
input/output failures. The exact eight-worker LLVM Debug build succeeded;
diagnostics catalog, source-line budget, and elaboration passed 3/3 in 0.36
seconds. The accumulated conversion test is 965 lines and
`hierarchy_types.cpp` is 1,638 lines. Task 9 is current; no sanitizer, Release,
full regression, commit, push, or CI inspection ran at this task boundary.

Task 9 adds `fsim.application.mixed_conversions`, a selector-hosted recursive
SV-to-VHDL-to-SV application covering width, combined signedness/width,
Boolean, integer, Bit2, and Logic4/Logic9 boundaries in one hierarchy. Each
build retains exactly 28 source-spanned conversion records and 11 owned adapter
processes; an empty specialized declaration span discovered by the provenance
assertions now falls back to the exact connection-identifier span. Interpreter,
LLVM O0, and LLVM O2 agree on `00001010`, `00001010`, `11111010`, `1`, signed
`-1`, `0110`, and `01XZ`, as well as a VHDL `boundary_probe` debugger local and
normalized VCD. Both optimization levels prove cold misses/stores, exact warm
hits, stable specialization keys, then a leaf-only source edit changes the
first result to `00001011`, changes cache identity, incurs native misses, and
again matches the interpreter. The application completes in 0.75 seconds.
ML-005 and ML-006 are now `execute` rows with positive, negative, elaboration,
and runtime evidence; the new `fsim.v1-mixed-conversion-matrix` gate requires
both ordered rows and forbids empty evidence columns. Diagnostics catalog,
source-line budget, the matrix gate, elaboration, and the recursive application
passed 5/5 in 1.12 seconds. The application test is 445 lines, the accumulated
conversion elaboration test is 965 lines, and `hierarchy_types.cpp` is 1,641
lines. Task 10 is current; no sanitizer, Release, full regression, commit, push,
or CI inspection ran at this task boundary.

Task 10 closes the batch on the final lifetime-safe implementation. The first
sanitizer pass exposed a heap use-after-free in `connect_ports`: appending an
owned adapter signal could reallocate `signal_info_` while the conversion path
retained a reference to the actual signal metadata. Copying that small metadata
record across adapter construction removes the invalid vector reference; the
focused sanitizer elaboration test then passed in 2.34 seconds. Final exact
eight-worker builds succeeded, LLVM Debug passed 78/78 tests in 181.04 seconds,
LLVM Release passed 78/78 in 161.04 seconds, and ASan/UBSan with leak detection
disabled for the managed ptrace environment passed 75/75 in 331.67 seconds.
Scoped locals remained quick at 0.81, 0.83, and 0.50 seconds respectively, and
the recursive mixed-conversion application passed in 0.76, 0.70, and 1.08
seconds. The full suites include the diagnostics catalog, source-line budget,
VHDL inventory, and both v1 matrix gates. Batch 121 is the single accumulated
commit/push checkpoint and, because it is not a tenth-batch boundary, requires
no GitHub Actions inspection.

### Batch 122 — mixed-language construction, drivers, and phase semantics — Complete

The current ten implementation tasks are:

1. **Complete.** Audit VHDL/SystemVerilog/SystemC construction actuals,
   cross-language driver ownership and resolution, delay propagation,
   scheduler phases, diagnostics, and ML-007/ML-008/ML-010 evidence; define the
   bounded positive and failure matrix.
2. **Complete.** Complete SystemVerilog parameter overrides transferred into
   VHDL value generics, including named/ordered association, type conversion,
   defaults, dependent port shapes, and specialization identity.
3. **Complete.** Complete VHDL generic maps transferred into SystemVerilog value
   parameters, including case rules, explicit/named values, defaults, width and
   signedness semantics, dependent generates, and specialization identity.
4. **Complete.** Complete supported Boolean, integer, packed logic, string, and
   SystemC construction-actual transfer in every hierarchy direction with
   canonical typed provenance and cold/warm/edit cache behavior.
5. **Complete.** Make cross-language input/output/buffer/inout driver ownership
   explicit through recursive aliases and adapters, admitting one logical
   forwarded writer while rejecting sibling, overlapping, and read-only writes.
6. **Complete.** Complete mixed VHDL resolved-signal and SystemVerilog wired-net
   multiple-driver behavior, including Logic9/Logic4 collapse, high impedance,
   update fanout, resolver selection, and deterministic conflict diagnostics.
7. **Complete.** Preserve zero and positive boundary delays plus VHDL
   inertial/transport/reject and SystemVerilog transition-delay behavior across
   adapters without duplicate, lost, or prematurely visible transactions.
8. **Complete.** Complete the cross-language active, inactive, NBA/update, and
   postponed phase lattice, including recursive feedback, same-slot races,
   stable source order, debugger stops, callbacks, and VCD observation.
9. **Complete.** Prove the combined construction/driver/timing matrix through
   recursive mixed hierarchy, interpreter, LLVM O0/O2, cold/warm/edit cache,
   debugger, VCD, provenance, and exact positive/negative diagnostics; close
   ML-008 and ML-010 in the feature matrix.
10. **Complete.** Update matrix/diagnostics/docs, pass sanitizer, source/catalog,
    and full Debug/Release gates, then create and push the single Batch 122
    checkpoint. This is not a mandatory CI-inspection boundary.

Batch status is **complete**. Keep this exact ten-task
list current in both the official plan and this handoff. Tasks 1 through 9 use
one accumulated dirty worktree with focused eight-worker Debug builds and
tests; Task 10 owns the sanitizer, full regressions, documentation, commit, and
push gate. GitHub builds use parallelism four, and Batch 122 does not require a
non-documentation CI inspection.

Task 1 confirms that the shared specialization path already transfers bounded
scalar integer construction actuals before port-shape checks in both HDL
directions. Existing applications prove SystemVerilog parameters into VHDL
generics and VHDL generic maps into SystemVerilog parameters, including mixed
named/positional language rules, defaults and derived widths, specialization
values, interpreter/O2 behavior, and cold/warm native keys. The append-only
SystemC schema likewise supports signed 64-bit integer, natural, positive,
Boolean, and bit construction values in both hierarchy directions. The
remaining construction gaps are complete target-typed Boolean/integer/packed
logic handling, cross-language string transfer, typed provenance, exact
edit-cache matrices, and unified failures; non-value VHDL interface generics
and SystemVerilog type parameters intentionally remain same-language.

Driver validation currently groups per-process whole/slice regions by final
scheduler signal. Native `std_logic`/`std_logic_vector`, `wire`/`tri`, explicit
resolver selection, and four-state resolution execute, but conversion adapters
own separate formal signals and processes without a retained logical-driver
chain. Recursive converted writers, read-only aliases, resolved adapters, and
cross-language inout ownership therefore lack one proof, while `wand`/`wor`
families still end in `FSIM-ELAB-DRV-002`. The runtime has stable active,
inactive, update, and postponed phases, and zero/positive delayed writes use
the common update scheduler; however ML-008 lacks an atomic recursive boundary
matrix combining delayed VHDL transactions, SystemVerilog NBA/update work,
adapter deltas, feedback, debugger stops, callbacks, and VCD observation.
Exact LLVM Debug elaboration, the main application, resolution, mixed
conversions, and runtime passed 5/5 focused tests in 16.89 seconds (0.17,
15.86, 0.09, 0.76, and 0.01 seconds). Task 2 is current; this begins the
intentional accumulated Batch 122 dirty worktree, with no sanitizer, Release,
full regression, commit, push, or CI inspection at this task boundary.

Task 2 confirms and locks the shared target-specialization path for
SystemVerilog-parent/VHDL-child construction rather than adding a parallel
foreign-parameter mechanism. A dedicated 176-line elaboration matrix proves
case-insensitive named overrides, ordered positional overrides, omitted
defaults, SystemVerilog one-bit values converted to VHDL Boolean, signed values
checked against a VHDL integer subtype, a dependent `Last := Width - 1` generic
and port shape, canonical specialization values/identities, and exact runtime
outputs for three independently specialized children. A noncanonical Boolean
actual is rejected by `FSIM-ELAB-GENERIC-008`. The existing mixed application
continues to prove interpreter/O2 execution, dependent port width, and cold/
warm native specialization keys. Direct packed VHDL value-generic syntax
remains deliberately in Task 4's typed construction slice. The exact
eight-worker LLVM Debug build succeeded; diagnostics catalog, source-line
budget, elaboration, and the main application passed 4/4 tests in 15.95 seconds
(0.08, 0.13, 0.17, and 15.57 seconds). Task 3 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this task
boundary.

Task 3 locks the reverse VHDL-parent/SystemVerilog-child path in the same
dedicated construction matrix. Named and positional VHDL generic maps now have
evidence against target-typed SystemVerilog `int`, one-bit `bit`, unsigned
four-bit, and signed eight-bit parameters. The target conversion truncates 18
to four-bit 2, preserves signed -3, applies omitted defaults, derives
`LAST = WIDTH - 1`, selects the matching generate branch, materializes the
dependent output shape, and retains distinct canonical `svconst-v1` identities
for all value and local parameters. Three child specializations execute exact
bit-vector and generated outputs. A VHDL name that case-insensitively matches
both `WIDTH` and `width` on a foreign SystemVerilog target is rejected by
`FSIM-ELAB-PARAM-009`. The combined construction test remains 357 lines. The
exact eight-worker LLVM Debug build succeeded; diagnostics catalog,
source-line budget, elaboration, and the main interpreter/O2 construction
application passed 4/4 tests in 16.01 seconds (0.08, 0.12, 0.17, and 15.64
seconds). Task 4 is current; no sanitizer, Release, full regression, commit,
push, or CI inspection ran at this task boundary.

Task 4 completes the bounded typed-construction slice without changing the
public SystemC C ABI. Direct VHDL `bit_vector`, `std_logic_vector`, and
`std_ulogic_vector` value generics up to 64 known bits now pass the frontend's
existing specialization path; wider, composite, or unknown/high-impedance
construction values remain checked failures. A VHDL string literal may now
target a SystemVerilog string parameter and retains its existing
`svstring-v1` byte identity, while VHDL-target Boolean, integer, and packed
values receive cross-language-only `vhdlconst-v1` identities carrying domain,
width, signedness, nominal type, declared range, and value. This avoids
perturbing same-language non-value generic identities. SystemC construction
continues to use its append-only signed-64-bit schema for integer, natural,
positive, Boolean, and bit values; HDL-parent construction in both languages
now retains `systemcconst-v1` type/value identities beside the ABI-neutral
integer values. The 446-line construction test proves packed values, strings,
all five SystemC scalar kinds, subtype failures, dependent shapes, generated
behavior, and runtime results in both HDL directions. The exact eight-worker
LLVM Debug build succeeded; frontend, diagnostics catalog, source-line budget,
elaboration, the main mixed/SystemC construction application, and the string
parameter application passed 6/6 tests in 15.83 seconds (0.02, 0.08, 0.12,
0.17, 15.30, and 0.14 seconds). Task 5 is current; no sanitizer, Release, full
regression, commit, push, or CI inspection ran at this task boundary.

Task 5 makes connected scalar/vector input ports read-only inside the child
specialization while leaving boundary adapter processes as the only legal
writers of their owned formal signals. A dedicated mixed-driver elaboration
matrix proves one recursive SystemVerilog-to-VHDL-to-SystemVerilog writer
through two narrowing adapters, exact interpreter propagation from two to four
to eight bits, and one conversion process per boundary. Two sibling VHDL
writers converted into the same SystemVerilog actual are rejected by the
existing logical-driver validation, and a VHDL child assignment through an
input port is rejected by `FSIM-ELAB-SVIFACE-006`; its catalog text now covers
both input ports and modport input members. The exact eight-worker LLVM Debug
build succeeded; diagnostics catalog, source-line budget, and elaboration
passed 3/3 tests in 0.37 seconds (0.08, 0.12, and 0.17 seconds). Task 6 is
current; no sanitizer, Release, full regression, commit, push, or CI inspection
ran at this task boundary.

Task 6 adds native `wand`/`triand` and `wor`/`trior` resolver kinds to SimIR
and admits the complete SystemVerilog net-type family at declaration parsing.
Wired resolution retains `Z` when every process releases a bit, otherwise
treats `Z` as the AND/OR identity and applies four-state logical dominance per
bit. Native resolver selection now admits multiple mixed-language boundary
drivers without requiring a redundant binding resolver, while unresolved
variables continue to receive the existing deterministic multiple-driver
diagnostics. The mixed-driver matrix proves VHDL Logic9 `0`, `1`, and `Z`
drivers collapsing into six SystemVerilog wired nets with conflict, release,
and all-high-impedance outcomes; the reverse VHDL `std_logic` matrix proves
SystemVerilog Logic4 conflict/release resolution and concurrent output fanout.
The obsolete `FSIM-ELAB-DRV-002` unsupported-policy diagnostic was removed.
The exact eight-worker LLVM Debug build succeeded; frontend, diagnostics
catalog, source-line budget, elaboration, the existing compiled resolution
application, and runtime passed 6/6 tests in 0.48 seconds (0.02, 0.08, 0.11,
0.16, 0.09, and 0.01 seconds). Task 7 is current; no sanitizer, Release, full
regression, commit, push, or CI inspection ran at this task boundary.

Task 7 extends the mixed resolution application with a recursive
SystemVerilog/VHDL/SystemVerilog timing hierarchy and six width-conversion
adapters. Zero-delay VHDL transport reaches the eight-bit SystemVerilog actual
in the same timestamp without duplicate publication. Positive VHDL default
inertial, explicit `reject 2 ps inertial`, and transport transactions preserve
their exact 5/15/16/25 ps cancellation or pulse histories through the outer
adapter. A nested SystemVerilog transition-delay leaf preserves its 3 ps fall,
12 ps rise, and 24 ps turnoff publications through one-to-four and four-to-eight
mixed adapters, including final high impedance. The application locks the
initial partial-domain publications as well as the absence of premature pulse
visibility. The exact eight-worker LLVM Debug build succeeded; diagnostics
catalog, source-line budget, elaboration, resolution application, and runtime
passed 5/5 tests in 0.45 seconds (0.08, 0.11, 0.16, 0.09, and 0.01 seconds).
Task 8 is current; no sanitizer, Release, full regression, commit, push, or CI
inspection ran at this task boundary.

Task 8 extends that hierarchy with a SystemVerilog active blocking write,
inactive `#0` write, and two same-slot NBA writes whose later source-order value
wins in the update phase. Exact callback histories prove `00`, `01`, then `11`
on the leaf in delta zero, one-to-four and four-to-eight adapter publications in
deltas one and two, and a VHDL zero-delay transport fanout in delta three.
`$strobe` observes the NBA winner once in the postponed phase. A second
two-adapter SystemVerilog/VHDL feedback loop deterministically advances from
unknown through 0, 1, 2, and 3 and quiesces at delta 16. Independent signal
observers agree exactly, VCD records all external and internal boundary nodes,
and `$stop` pauses at 1 ps for debugger inspection before a successful resume
to the 30 ps design finish. The exact eight-worker LLVM Debug build succeeded;
diagnostics catalog, source-line budget, elaboration, resolution application,
and runtime passed 5/5 tests in 0.48 seconds (0.08, 0.12, 0.17, 0.10, and 0.01
seconds). Task 9 is current; no sanitizer, Release, full regression, commit,
push, or CI inspection ran at this task boundary.

Task 9 combines an explicit SystemVerilog Boolean construction override with
the complete driver/timing hierarchy and verifies its canonical Boolean
`vhdlconst-v1` identity. The application now captures the entire phase,
feedback, delay, postponed-output, debugger, independent-callback, final-value,
VCD, specialization-key, and boundary-conversion state under the interpreter
and LLVM O0/O2. Each optimization proves a cold native-cache fill and exact
warm hits, then edits the VHDL source, observes the changed zero-delay result,
and requires a changed specialization-key set plus at least one native miss
while retaining construction identity and boundary topology. The feature
matrix now marks ML-008 and ML-010 executable and expands ML-007 for native
wired-AND/OR resolution; the language-support inventory no longer lists wired
resolution as a gap. The exact seven-test focused gate passed in 1.59 seconds:
diagnostics catalog 0.08, source-line budget 0.11, mixed matrix 0.01,
elaboration 0.17, resolution 0.45, mixed conversions 0.75, and runtime 0.01
seconds. Task 10 is current; no sanitizer, Release, full regression, commit,
push, or CI inspection ran at this task boundary.

Task 10 is complete. The LLVM-disabled ASan/UBSan regression passed all 75
tests in 329.57 seconds with no sanitizer findings; leak detection alone was
disabled for the locally traced run because LeakSanitizer cannot operate under
the workspace tracer, while the CI preset retains leak detection. The exact
LLVM 22.1.8 warnings-as-errors Debug regression passed all 78 tests, including
`fsim.application.scoped_locals` in 0.81 seconds and the expanded resolution
application in 0.45 seconds. The corresponding Release build and 78-test
regression passed in 168.86 seconds, with scoped locals in 0.81 seconds and
resolution in 0.53 seconds. Both full suites include the diagnostics catalog,
source-line budget, IEEE inventory, and v1 matrix gates. Batch 122 closes as
one accumulated commit/push checkpoint and, because it is not a tenth-batch
boundary, has no GitHub Actions inspection.

### Batch 123 — SystemC named hierarchy and interfaces — Complete

The current ten implementation tasks are:

1. **Complete.** Audit SC-001 through SC-022 and the SystemC facade/ABI,
   hierarchy registry, DesignIR, debugger/API, and application evidence for
   named-object hierarchy, ports, exports, standard interfaces, and bounded
   custom metadata; define the exact positive and failure matrix.
2. **Complete.** Add bounded `sc_object` identity and introspection for modules,
   ports, exports, signals, primitive channels, events, and processes,
   including stable `name`, `basename`, `kind`, and parent ownership.
3. **Complete.** Preserve deterministic fully qualified names and construction
   order across native children, foreign HDL placeholders, factory roots, and
   repeated `sc_gen_unique_name` use, rejecting duplicate or invalid sibling
   names transactionally.
4. **Complete.** Complete parent/child object traversal and lookup through the
   append-only plug-in ABI and common hierarchy, with stable handles and no
   cross-build or destroyed-object leakage.
5. **Complete.** Complete typed `sc_in`, `sc_out`, and `sc_inout` binding policies
   across direct interfaces, signals, parent/child port chains, and HDL aliases,
   including direction, cardinality, cycle, skipped-parent, and unbound checks.
6. **Complete.** Complete `sc_export` binding and transitive resolution for the
   supported standard signal interfaces, including export-to-interface,
   export-to-export, port-to-export, read/write capability, and exact failures.
7. **Complete.** Materialize every supported SystemC named object in common
   DesignIR/API/debugger/VCD hierarchy with consistent source, kind, parent,
   signal identity, and lookup behavior across mixed-language boundaries.
8. **Complete.** Add metadata-only registration for bounded custom interface and
   primitive-channel kinds permitted by v1, preserving names and hierarchy
   while rejecting unsupported custom binding, value, or asynchronous-update
   behavior explicitly.
9. **Complete.** Prove the combined named hierarchy/port/export/interface matrix
   in compiled SystemC with interpreter and LLVM O0/O2 HDL peers, cold/warm/edit
   cache, lifecycle, callbacks, debugger, VCD, and exact negative diagnostics.
10. **Complete.** Update matrix/subset/diagnostics/docs, pass sanitizer,
    source/catalog and full Debug/Release gates, then create and push the single
    Batch 123 checkpoint. This is not a mandatory CI-inspection boundary.

Batch status is **complete**. Keep this exact ten-task
list current in both the official plan and this handoff. Tasks 1 through 9 use
one accumulated dirty worktree with focused eight-worker Debug builds and
tests; Task 10 owns the sanitizer, full regressions, documentation, commit, and
push gate. GitHub builds use parallelism four, and Batch 123 does not require a
non-documentation CI inspection.

Task 1 confirms that SC-001 through SC-022 already execute the bounded source
facade, typed factories, native and foreign child modules, per-category object
registration, standard signal interfaces, direct-parent port chains, typed
export chains, common signal aliases, lifecycle, process/event/channel
scheduling, and construction actuals. Registry descriptions retain stable
handles, local names, module parents, and declaration order, but the facade
exposes a name only for `sc_module`; there is no common `sc_object`, basename,
kind, parent, child traversal, or lookup surface. Duplicate detection is
category-local rather than one sibling object namespace, and
`sc_gen_unique_name` is a process-thread counter rather than a hierarchy-scoped
collision-aware allocator. DesignIR retains SystemC instance, port, event,
primitive-channel, signal, export, and process records, but only modules,
processes, and signal aliases reach the common API/debug/VCD object model.
Standard `sc_signal_in_if`/`sc_signal_inout_if` bindings are typed and
executable; arbitrary custom-interface calls and values remain outside v1, so
the bounded closure is metadata-only registration with exact rejection of
unsupported behavior. The exact eight-worker Debug build required no work;
elaboration, facade header, strict C ABI, plug-in loader, plug-in compiler,
main application, and SystemC datatype application passed 7/7 focused tests
in 20.17 seconds. Task 2 is current; this starts the intentional accumulated
Batch 123 dirty worktree with no sanitizer, Release, full regression, commit,
push, or CI inspection at this task boundary.

Task 2 adds a common facade `sc_object` base with stable `name()`, `basename()`,
`kind()`, and `get_parent_object()` identity. `sc_module`, `sc_in`, `sc_out`,
`sc_inout`, `sc_export`, `sc_signal`/`sc_prim_channel`, and `sc_event` now carry
that identity, and process registration retains a stable owned object with the
exact method/thread/cthread kind. Member ports and registered processes receive
fully qualified module-relative names and the module parent; standalone named
objects retain null parents. The append-only native ABI is unchanged. Header
tests prove the base relationships and exact module, port, signal, event,
channel, export, and method-process identities. The exact eight-worker Debug
build succeeded; facade header, strict C ABI, plug-in loader, main application,
diagnostics catalog, and source-line budget passed 6/6 focused tests in 16.42
seconds. Task 3 is current; no sanitizer, Release, full regression, commit,
push, or CI inspection ran at this task boundary.

Task 3 makes `sc_gen_unique_name` deterministic per module and base, skips
explicit sibling collisions, and preserves independent counters for separate
module instances. Facade construction now rejects invalid child basenames and
cross-kind duplicate sibling objects before registration. The native host
independently enforces the same one-namespace rule across ports, foreign/native
children, processes, events, primitive channels/signals, and exports, while
recognizing a typed `sc_signal` as the metadata promotion of its existing
primitive-channel handle. Qualified mixed-language root paths retain their
full `name()` and expose only the last component as `basename()`; native child
names remain local inputs and receive exactly one parent-qualified path.
Header tests cover explicit-name skipping, per-instance counters, invalid
names, and cross-kind duplicates, while the application retains its native
duplicate-child failure and compiled mixed hierarchy. The exact eight-worker
Debug build succeeded; elaboration, facade header, strict C ABI, plug-in loader,
and main application passed 5/5 focused tests in 16.20 seconds, followed by a
2/2 diagnostics/source gate in 0.20 seconds. Task 4 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this task
boundary.

Task 4 adds declaration-ordered `sc_object::get_child_objects()`, domain-scoped
`sc_find_object()` and top-level enumeration, and destructor removal so facade
lookups never retain destroyed pointers. Hierarchy domains use the active host
context, isolating simultaneously loaded build roots while keeping standalone
facade objects usable. The common `HierarchyRegistry` now returns copied,
ABI-neutral object metadata by stable handle, direct children in monotonically
allocated registration order, and absolute or root-relative path lookup whose
parent chain must reach the requested root. Modules, ports, foreign children,
processes, events, standalone primitive channels, promoted signals, exports,
and native modules are classified; signal promotion retains one handle rather
than duplicating its primitive channel. Header tests prove child order, lookup,
top-level membership, process discovery, and destruction cleanup. Loader tests
prove root/port/process/foreign-child metadata, nested roots, child ordering,
relative/absolute lookup, missing paths, and stable parent handles. The exact
eight-worker Debug build succeeded; facade header, strict C ABI, plug-in loader,
and main application passed 4/4 focused tests in 16.04 seconds, followed by a
2/2 diagnostics/source gate in 0.21 seconds. Task 5 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this task
boundary.

Task 5 completes the typed port policy independently in the facade and native
host. Existing templates retain exact interface value types and prevent
cross-direction chains at compile time; host object metadata now also retains
port direction and rejects a child input→parent output, child output→parent
input, or any inout→non-inout chain even for a raw ABI plug-in. Encoding and
width equality, same/direct-parent signal scope, direct-parent port/export
scope, one-target cardinality, idempotent rebinding, and structural cycle/
skipped-parent rejection remain enforced. Common elaboration now emits
`FSIM-ELAB-BIND-058` for every unbound native-child port while leaving selected
root factory ports and HDL-connected ports as external aliases. A raw compiled
plug-in probe must observe rejection of an output→input bind before accepted
input→input and output→output binds can build two SystemC levels. A separate
native fixture requires exactly two unbound-port diagnostics; existing direct
port, signal, export, HDL alias, conflicting-target, cycle, and deep-parent
cases remain green. The exact eight-worker Debug build succeeded, and the
expanded main application passed in 16.57 seconds after the preceding 7/7
SystemC/elaboration/catalog/source focused gate passed in 16.64 seconds. Task 6
is current; no sanitizer, Release, full regression, commit, push, or CI
inspection ran at this task boundary.

Task 6 appends an optional `set_export_writable` host callback without changing
the v1 ABI prefix. Current facade exports set read-only capability for
`sc_signal_in_if<T>` and writable capability for `sc_signal_inout_if<T>` before
binding; legacy plug-ins retain the former writable default. Registry binding
now rejects a writable export targeting a read-only export and an output/inout
port targeting a read-only export, while permitting read-only narrowing onto a
writable signal/export. Existing exact encoding/width, same/direct-parent,
one-target, cycle, unknown-handle, and unbound checks remain active. Capability
travels through immutable factory descriptions and elaborated DesignIR export
records. The raw compiled probe requires writable→read-only export and
output→read-only-export rejection before read-only/writable signal bindings and
an input→read-only-export alias can build successfully. The real four-export
hierarchy proves two read-only and two writable records plus unchanged common
signal identity and execution. The exact eight-worker Debug builds succeeded;
diagnostics, source budget, elaboration, facade, strict C ABI, loader, and main
application passed 7/7 focused tests in 16.60 seconds. Task 7 is current; no
sanitizer, Release, full regression, commit, push, or CI inspection ran at this
task boundary.

Task 7 materializes modules, foreign HDL children, ports, processes, events,
primitive channels, signals, and exports as immutable named DesignIR objects.
Each record retains its stable native handle, exact common path and parent,
SystemC kind/type, optional process, and optional dense signal identity. The C
API appends event, channel, and export kinds, preserves distinct alias handles
while reading and writing their shared signal, and enumerates native SystemC
modules in the same scope tree as HDL instances. Debugger scope discovery now
retains value-less native hierarchy, while common signal lookup and production
VCD declarations include every value-bearing SystemC alias, including native
child ports. A compiled API factory proves exact root/child traversal and
lookup for all supported object categories; the mixed application proves
DesignIR identity, debugger navigation, VCD scopes, and nested alias values.
The exact eight-worker Debug build succeeded, and the main application plus C
API tests passed 2/2 in 18.82 seconds. Task 8 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this task
boundary.

Task 8 appends metadata-object registration and primitive-channel kind labeling
to the v1 host ABI while preserving its complete prefix. The facade adds a
metadata-only generic `sc_port<IF>`, records unsupported-interface
`sc_export<IF>` objects, and permits derived primitive channels to supply a
bounded explicit kind. Custom interfaces may expose a stable kind through
`IF::fsim_kind()`; otherwise they use `sc_interface`. These objects retain
stable native handles, exact names/parents, port/export/channel categories,
and custom type names through the registry, immutable factory description,
DesignIR, and C API, but intentionally have no dense signal. A compiled
factory proves positive custom port/export/channel metadata and exact
`FSIM-SC-A004` construction failure for custom binding. Runtime attempts at
custom value access or asynchronous primitive-channel update throw exact
unsupported errors and poison the simulation rather than silently executing.
The exact eight-worker Debug build succeeded; facade, strict C ABI, loader,
main application, C API, diagnostics catalog, and source budget passed 7/7
focused tests in 18.92 seconds. Task 9 is current; no sanitizer, Release, full
regression, commit, push, or CI inspection ran at this task boundary.

Task 9 adds one compiled mixed-language matrix that combines root and native-
child ports, standard read-only and writable exports, promoted signals, a
named event and method process, metadata-only custom port/export/channel
objects, and ordered lifecycle callbacks. The same fixture now proves
interpreter reference behavior, LLVM O0/O2 cold and warm native-cache parity,
source-edit invalidation, debugger traversal, and production VCD aliases while
retaining the exact custom binding, value-access, and asynchronous-update
failures from Task 8. The exact eight-worker Debug build succeeded; the main
application passed in 28.10 seconds, and the expanded application, C API,
C-header, facade, strict C ABI, loader, diagnostics-catalog, and source-budget
gate passed 8/8 in 29.61 seconds. Task 10 is current; no sanitizer, Release,
full regression, commit, push, or CI inspection has run yet.

Task 10 is complete. The LLVM-disabled ASan/UBSan regression passed all 75
tests in 375.25 seconds with no sanitizer findings; leak detection alone was
disabled for the locally traced run because LeakSanitizer cannot operate under
the workspace tracer, while the CI preset retains leak detection. Its initial
warnings-as-errors build exposed and repaired three stale aggregate fixtures
that omitted the newly retained foreign-child handles and trailing identity
field. The exact LLVM 22.1.8 warnings-as-errors Debug regression then passed
all 78 tests in 200.08 seconds, including `fsim.application.scoped_locals` in
0.82 seconds and resolution in 0.47 seconds. The corresponding Release build
and 78-test regression passed in 177.78 seconds, with scoped locals in 0.84
seconds and resolution in 0.44 seconds. Both full suites include the
diagnostics catalog, source-line budget, IEEE inventory, and v1 matrix gates.
Batch 123 closes as one accumulated commit/push checkpoint and, because it is
not a tenth-batch boundary, has no GitHub Actions inspection.

### Batch 124 — SystemC scheduling and lifecycle — Complete

The current ten implementation tasks are:

1. **Complete.** Audit SC-007, SC-008, and SC-012 through SC-017 across the
   facade, append-only ABI, hierarchy registry, common scheduler, lifecycle,
   fibers, and application evidence; define the exact missing sensitivity,
   timeout, event, update-phase, and failure matrix.
2. **Complete.** Complete static sensitivity for ports, internal signals, named
   events, and positive/negative edge finders across methods, threads, and
   clocked threads, including deterministic deduplication and exact invalid or
   unbound-object diagnostics.
3. **Complete.** Complete dynamic `next_trigger` for time, event, OR/AND event
   lists, and timed event/list timeouts, with one replacement wait per method
   invocation and deterministic tie handling.
4. **Complete.** Complete `wait` for time, zero time, event, OR/AND event lists,
   timed event/list timeouts, and plain static sensitivity across `SC_THREAD`
   and `SC_CTHREAD`, including repeated suspension and teardown.
5. **Complete.** Close immediate, delta, timed, delayed, replacement,
   cancellation, duplicate, and same-timestamp named-event scheduling across
   method/thread waiters with exact pending-state and tick-conversion failures.
6. **Complete.** Close primitive-channel update ordering and deduplication,
   including self/cross-channel requests, port reads/writes, event notification,
   callback containment, and requests made from process and update phases.
7. **Complete.** Complete root/native-child lifecycle ordering and isolation for
   cold/warm builds, natural quiescence, `$finish`, explicit stop/resume,
   teardown, callback state, and exact structural or scheduling rejections.
8. **Complete.** Prove deterministic common-kernel phase interactions among
   nested native methods/threads/channels/events and SystemVerilog/VHDL peers,
   including stable process/channel order and no recursive native execution.
9. **Complete.** Prove the combined scheduling/lifecycle matrix in compiled
   SystemC with interpreter and LLVM O0/O2 HDL peers, cold/warm/edit cache,
   debugger, VCD, callbacks, teardown, and exact negative diagnostics.
10. **Complete.** Update matrix/subset/diagnostics/docs, pass sanitizer,
    source/catalog and full Debug/Release gates, then create and push the single
    Batch 124 checkpoint. This is not a mandatory CI-inspection boundary.

Batch status is **complete**. The ten tasks used one accumulated worktree and
one Task 10 sanitizer, full-regression, documentation, commit, and push gate.
GitHub builds use parallelism four, and Batch 124 does not require a non-
documentation CI inspection.

Task 1 confirms that the common kernel already executes initialized and
`dont_initialize()` methods, basic thread/clocked-thread fibers, scalar static
port/signal/event sensitivity with edge qualifiers, exclusive time/event/
OR-list/AND-list dynamic waits, immediate/delta/timed/delayed event scheduling
and cancellation, deduplicated channel updates, module-local signal updates,
and ordered root/native-child lifecycle callbacks. Static registration already
deduplicates identical object/edge pairs and rejects cross-module objects or an
edge-qualified named event. The missing v1 closure is timed event/list timeout
selection, explicit method-only versus thread-only API checks, expanded static
method/thread/CTHREAD coverage, complete same-timestamp event/channel phase
ordering, and lifecycle stop/teardown/rejection evidence. The exact eight-
worker Debug build required no work; elaboration, facade, strict C ABI, loader,
compiler, main application, and runtime passed 7/7 focused tests in 30.19
seconds. Task 2 is current; this starts the intentional accumulated Batch 124
dirty worktree with no sanitizer, Release, full regression, commit, push, or CI
inspection at this task boundary.

Task 2 expands the real compiled fiber fixture so a method is statically
sensitive to one named event registered twice, a clocked thread retains its
positive-edge finder, and a `dont_initialize()` thread waits on one negative-
edge finder registered twice. DesignIR canonicalizes each repeated object/edge
pair to one sensitivity, and the HDL-driven run proves two named-event method
invocations, two positive-edge CTHREAD resumptions, and one negative-edge
thread resumption. Manual immutable descriptions separately prove exact
`FSIM-ELAB-BIND-043`, `-044`, and `-045` rejection for an unknown object, an
invalid edge encoding, and a nonscalar edge target. Existing internal-signal,
port, unbound-native-port, and cross-module registry cases remain green. The
exact eight-worker Debug build succeeded; elaboration, application, facade,
strict C ABI, loader, diagnostics catalog, and source budget passed 7/7 in
29.55 seconds. Task 3 is current; no sanitizer, Release, full regression,
commit, push, or CI inspection ran at this task boundary.

Task 3 appends one `wait_event_timeout` host callback and carries an optional
timeout through the ABI-neutral SystemC suspension and common alternate-
executor boundary. Event/list waits register their canonical dynamic fanout
and reuse the scheduler's generation-checked timeout machinery; an event wake
invalidates its deadline, a timeout removes its event registrations, and stale
heap entries remain deterministic no-ops. The facade adds timed single-event,
OR-list, and AND-list `next_trigger` overloads, restricts every `next_trigger`
form to `SC_METHOD`, and preserves last-call replacement within one callback.
A real compiled method matrix proves event wins, timeout wins, a same-timestamp
tie, duplicate list canonicalization, AND progress, replacement, and
interpreter/compiled parity at exact observed producer states. The host ABI
offset assertion remains append-only. The exact eight-worker Debug build
succeeded; application, runtime, facade, strict C ABI, diagnostics catalog,
and source budget passed 6/6 in 27.98 seconds. Task 4 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this boundary.

Task 4 exposes the same timed event/list host suspension to `SC_THREAD` and
`SC_CTHREAD` through new time-plus-event, OR-list, and AND-list `wait`
overloads, while retaining exact rejection from `SC_METHOD`. The compiled fiber
fixture now interleaves an event-won single wait, a timeout-won single wait, an
event-won OR wait, and a timeout-won partially satisfied AND wait across four
ordinary C++ stack resumptions. Existing finite time, zero-time, repeated named
event, plain positive/negative static sensitivity, and clocked-thread waits
remain green, and the host stops only after every new suspension has resumed.
All event fanout and timeout generations are cleared before fiber teardown.
The exact eight-worker Debug build succeeded; application, runtime, facade,
strict C ABI, loader, diagnostics catalog, and source budget passed 7/7 in
28.09 seconds. Task 5 is current; no sanitizer, Release, full regression,
commit, push, or CI inspection ran at this task boundary.

Task 5 closes named-event scheduling across the existing generation-checked
runtime replacement/cancellation tests, the dynamic method matrix, and the
expanded fiber fixture. Immediate notification, next-delta notification,
earliest timed replacement, later timed no-op, explicit cancellation, strict
single-pending `notify_delayed`, and stale heap generations now have joint
method/thread evidence. Stable process IDs make same-timestamp producer,
method, fiber, and timeout work deterministic without recursive native entry.
Two compiled negative factories additionally prove the exact unrepresentable-
project-tick failure and duplicate-pending delayed-notification failure poison
only their sessions. The exact eight-worker Debug build succeeded;
application, runtime, facade, strict C ABI, loader, diagnostics catalog, and
source budget passed 7/7 in 28.63 seconds. Task 6 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this boundary.

Task 6 combines the kernel's stable handle-ordered channel test with two real
compiled primitive channels. Process-phase duplicate requests coalesce; a
self-request made during `update()` is ignored; a request from the later
channel to the earlier channel is deferred to the next delta; and port writes
commit through the shared update phase. The later channel also performs an
immediate named-event notification during update, deterministically waking a
method without recursive native entry and canceling the event's earlier timed
notification. Exact final value/update/event counters agree between the
interpreter and compiled engine. A separate throwing channel preserves its
original exception diagnostic, poisons only that simulation, and cannot escape
the native ABI. The exact eight-worker Debug build succeeded; application,
runtime, facade, strict C ABI, loader, diagnostics catalog, and source budget
passed 7/7 in 29.08 seconds. Task 7 is current; no sanitizer, Release, full
regression, commit, push, or CI inspection ran at this task boundary.

Task 7 retains exact parent-before-child elaboration/start and child-before-
parent end ordering while extending the real lifecycle fixture to three
independent cold/warm roots. Interpreter and compiled `$finish` runs agree; a
pre-requested external stop starts SystemC once without ending it, then clear/
resume reaches the same terminal state and calls end exactly once; and a
direct SystemC top reaches natural quiescence at time zero. Destruction still
ends any started nonterminal root after shutting down fibers. Runtime access
from lifecycle callbacks is now explicitly context-checked: event notify,
delayed notify, and cancel cannot dereference a missing process context, and a
raw lifecycle suspension is rejected. Compiled negative roots prove event
scheduling and `next_trigger` rejection poison only their simulations, while
the existing elaboration/end exception cases remain isolated. The exact
eight-worker Debug build succeeded; application, runtime, facade, strict C
ABI, loader, diagnostics catalog, and source budget passed 7/7 in 30.15
seconds. Task 8 is current; no sanitizer, Release, full regression, commit,
push, or CI inspection ran at this task boundary.

Task 8 drives the same compiled nested method, thread, clocked-thread, named-
event, timeout, and channel phase interactions from both SystemVerilog and
VHDL peers. The VHDL host now supplies matching positive and negative clock
edges through the common scheduler and naturally drains at tick 5; its exact
static, named-event, event-won, timeout-won, and clocked-thread results match
the SystemVerilog host. Existing stable handle ordering, deferred cross-channel
updates, and update-phase event notification prove that neither peer can cause
recursive native execution. The exact eight-worker Debug build succeeded;
application, runtime, facade, strict C ABI, loader, diagnostics catalog, and
source budget passed 7/7 in 31.78 seconds. Task 9 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this task
boundary.

Task 9 adds one combined SystemVerilog and VHDL peer around the fiber-thread,
primitive-channel, and ordered-lifecycle roots. Interpreter references and
LLVM O0/O2 cold/warm pairs agree on all eleven observable signals, terminal
time, status, and callback count. A SystemC comment edit invalidates the
project/plugin cache while retaining exact native HDL cache hits; the debugger
and VCD expose the nested thread plus peer-visible channel and lifecycle
signals. Compiled channel-update, lifecycle-suspension, and terminal-lifecycle
failures retain their exact messages, poison only their sessions, and a fresh
post-teardown interpreter run proves isolation. The new 322-line scheduling
partition leaves the existing integration and generated-source partitions at
1,844 and 1,619 lines. The exact eight-worker Debug build succeeded;
application, runtime, facade, strict C ABI, loader, diagnostics catalog, and
source budget passed 7/7 in 53.28 seconds. Task 10 is current; it owns the
single accumulated sanitizer, full regression, documentation, commit, and
push gate, with no CI inspection at this non-boundary batch.

Task 10 is complete. The release authority and SystemC subset now record timed
event/list waits, method replacement, stable channel phases, lifecycle stop/
resume and teardown, and the combined SV/VHDL differential. The diagnostics
catalog covers 1,622 production codes, and all 398 authored sources pass the
2,000-line gate. The LLVM-disabled ASan/UBSan suite passed 75/75 in 438.21
seconds with leak detection disabled only for the locally traced run; the main
application, containers, and scoped locals took 140.54, 219.83, and 0.49
seconds. The exact LLVM 22.1.8 warnings-as-errors Debug suite passed 78/78 in
225.27 seconds, with the application at 54.43 seconds, containers at 111.04,
resolution at 0.54, and scoped locals at 0.84. Release passed 78/78 in 205.80
seconds, with the application at 52.34 seconds, containers at 94.22,
resolution at 0.53, and scoped locals at 0.86. Batch 124 closes as one
accumulated checkpoint and has no GitHub Actions inspection because it is not
a tenth-batch boundary.

### Batch 125 — SystemC compiler, cache, and portability — Complete

The current ten implementation tasks are:

1. **Complete.** Audit the SystemC source compiler, dependency scanner,
   persistent plug-in cache, loader, registry, native-cache composition, fiber
   backends, and existing Linux/Windows evidence; define every missing compiler,
   cache, lifecycle, error, and portability case.
2. **Complete.** Make plug-in compile fingerprints canonical across ordered
   sources, content, include directories, definitions, language mode, compiler
   identity/version/target, options, ABI version, and selected fiber backend.
3. **Complete.** Complete transitive dependency fingerprints and invalidation for
   edited, added, removed, generated, missing, and system headers across GNU-
   style and MSVC dependency discovery, including paths containing spaces.
4. **Complete.** Make persistent cache lookup/publication transactional and
   concurrency-safe, with deterministic recovery from missing, truncated,
   corrupted, stale, or incompatible metadata and shared-library artifacts.
5. **Complete.** Prove loaded-image, registration, factory, root, fiber, and
   callback ownership across cold/warm/edit builds, concurrent independent
   sessions, terminal and nonterminal teardown, rebuild, and unload ordering.
6. **Complete.** Close exact compile, link, dependency, load, entry-point, ABI,
   initialization, registration, construction, destruction, and callback error
   containment without partial registration or stale cache publication.
7. **Complete.** Compose plug-in/factory/construction/hierarchy identity into
   interpreter, LLVM O0/O2, and debug native-cache behavior, preserving valid
   reuse while preventing stale code, objects, callbacks, or native handles.
8. **Complete.** Harden Windows process invocation, quoting, response paths,
   DLL/PDB/runtime discovery, compiler diagnostics, PE/MASM fiber selection,
   and thread teardown while retaining portable Linux behavior.
9. **Complete.** Add a combined compiler/cache/lifecycle matrix with Linux
   execution and Windows-targeted Debug/Release/LLVM/thread regression coverage,
   cold/warm/edit/corruption/concurrency cases, and exact negative diagnostics.
10. **Complete.** Update matrix/subset/diagnostics/docs, pass sanitizer,
    source/catalog and full Debug/Release gates, then create and push the single
    Batch 125 checkpoint. This is not a mandatory CI-inspection boundary.

Batch status is **complete**. This exact ten-task list remains the durable
Batch 125 record in both the official plan and this handoff. Tasks 1 through 9
used one accumulated dirty worktree with focused eight-worker Debug builds and
tests; Task 10 owns the sanitizer, full regressions, documentation, commit, and
push gate. GitHub builds use parallelism four, and Batch 125 does not require a
non-documentation CI inspection.

Task 1 confirms that the current compiler already hashes ordered source
contents, explicit include/define sequences, ABI/host format, compiler binary,
explicit linked libraries, and GCC-emitted transitive dependency closures. It
uses per-key locks, checksum-validated atomic artifacts, post-compile identity
verification, unique MSVC object/PDB paths and response files, buffered plug-in
initialization, and fiber-before-module-before-library teardown. Existing tests
cover spaces and literal arguments, stale locks, header/source/library edits,
volatile macros, implicit GCC roots, malformed options, missing compilers,
changed-during-compile rejection, initialization exceptions, PE/MASM source
selection, and the four Windows LLVM/MSVC build shapes. The remaining closure
is compiler/frontend/linker plus environment identity, compiler-emitted MSVC
dependency closure, corruption/concurrent publication, transactional registrar
rollback, complete load/factory/destructor errors, native-cache coupling, and
executable Windows lifecycle/thread evidence. The exact eight-worker Debug
build required no work; facade, loader, compiler, application, diagnostics
catalog, and source budget passed 6/6 in 55.95 seconds. Task 2 is current; this
starts the accumulated Batch 125 dirty worktree with no sanitizer, Release,
full regression, commit, push, or CI inspection at this task boundary.

Task 2 versions the compile fingerprint as `systemc-compiler-v2` and records
the fixed C++20 source contract, runtime/SystemC ABI, host toolchain/format,
MSVC CRT choice, and exact configured Boost.Context backend. Persistent reuse
now requires hashing the resolved compiler executable rather than falling back
to size/mtime alone. Ordered compiler-relevant environment values cover PATH,
GCC include/library/program roots and reproducible-time input on GCC-like
hosts, plus MSVC include/library/toolset/SDK/CL state on Windows. Existing
ordered include, definition, raw-option, explicit-library, and source content
sequences remain part of the key; raw options remain deliberately
noncacheable. Tests prove stable repeats plus source-order, definition, and
toolchain-environment divergence, including restoration to the original key.
The exact eight-worker Debug builds succeeded; compiler, application,
diagnostics catalog, and source budget passed 4/4 in 54.51 seconds, with the
application at 52.72 seconds. The compiler implementation and test remain at
393 and 860 lines. Task 3 is current; no sanitizer, Release, full regression,
commit, push, or CI inspection ran at this task boundary.

Task 3 adds compiler-emitted MSVC dependency closure through
`/sourceDependencies` JSON while retaining the conservative manifest scanner
when the selected compiler lacks the option, compilation cannot produce a
report, or the report is malformed. The bounded parser accepts UTF-8 BOMs,
JSON escapes, absolute paths containing spaces, and the documented `Source`,
`Includes`, PCH, imported-module BMI, and imported-header-unit Header/BMI
inputs. Every readable dependency is normalized, sorted, deduplicated, and
content-hashed; source files are excluded from the duplicate dependency list,
and binary IFC/PCH images are hashed without loading them into the volatile
predefined-macro scanner. The shared GNU/MSVC finalizer now applies the same
volatile-macro and deterministic ordering rules to both compiler-emitted
formats. A portable fake `cl.exe` fixture proves escaped/BOM JSON, header and
BMI edits, paths with spaces, a removed dependency, and malformed-report
fallback on Linux, while native Windows plans now require system-header
closure to remain cacheable. The exact eight-worker Debug build succeeded;
compiler, application, diagnostics catalog, and source budget passed 4/4 in
55.86 seconds, with the compiler at 1.61 seconds and application at 54.05
seconds. The dependency implementation and compiler test remain at 848 and
1,003 lines. Task 4 is current; no sanitizer, Release, full regression,
commit, push, or CI inspection ran at this task boundary.

Task 4 replaces the loose checksum sidecar with a versioned SystemC artifact
commit record containing the exact cache key, shared-library size, and SHA-256.
The library is atomically installed first and the metadata rename is the final
commit marker, so readers accept only one complete matching pair; a failed
metadata commit removes the uncommitted library. Under the existing process-
aware per-key lock, rebuild preparation removes abandoned build outputs,
objects/PDBs, metadata temporaries, and legacy checksum temporaries, while a
successful publication retires the legacy checksum. Missing libraries or
metadata, zero/truncated libraries, truncated/corrupt/incompatible metadata,
size or checksum mismatches, and stale staging are deterministic misses and
self-repair through one writer. The compiler fixture forces every recovery
shape and launches three simultaneous callers after deleting the committed
pair; exactly one reports a cold build, both waiters report validated hits,
and the following lookup remains warm. The exact eight-worker Debug build
succeeded; compiler, application, diagnostics catalog, and source budget
passed 4/4 in 56.24 seconds, with the compiler at 2.42 seconds and application
at 53.61 seconds. The compiler facade, cache implementation, dependency
implementation, and compiler test remain at 390, 1,293, 848, and 1,099 lines.
Task 5 is current; no sanitizer, Release, full regression, commit, push, or CI
inspection ran at this task boundary.

Task 5 closes loaded-image and executable-state ownership. Live-registry
move-assignment no longer lets `unique_ptr` destroy an old implementation
without its lifecycle protocol: the shared reset path first resumes and stops
suspended fibers, gives each still-started root one reverse-order best-effort
`end_of_simulation`, destroys module objects in reverse construction order,
then unloads the plug-in before releasing its host context and registry state.
Explicitly ended and poisoned roots are not repeated. `Plugin` itself is now
nonmovable so default member assignment cannot discard its retained host table
before unloading the old image. A read-only platform query provides executable
proof of image residency without changing its loader reference count.

The sample plug-in now exports a bounded lifecycle event probe and a minimal
suspending thread factory. The loader test proves explicit terminal ordering
for parent/nested roots, two simultaneously live independent registries, a
suspended fiber stopped before implicit terminal/destruction during move-
assignment, safe adoption and teardown of the second registry, and image
residency until the final owner is released followed by actual unload. The
existing application matrix continues to cover cold/warm native reuse and an
edited SystemC source image. The exact eight-worker Debug build succeeded;
loader, compiler, application, diagnostics catalog, and source budget passed
5/5 in 56.27 seconds, with the loader at 0.00, compiler at 2.25, and application
at 53.82 seconds. The dynamic-loader, hierarchy, plug-in loader test, and
sample plug-in remain at 162, 938, 453, and 321 lines. Task 6 is current; no
sanitizer, Release, full regression, commit, push, or CI inspection ran at
this task boundary.

Task 6 makes registration replay transactional for the fsim hierarchy. Buffer
callbacks validate unique factory names, factory-before-parameter order,
parameter uniqueness, construction types, and default ranges, and remember
every rejected callback. A plug-in that ignores rejection and returns success
is rejected before any caller registrar callback runs. Hierarchy loading now
replays into a separate staging implementation, swaps the factory table only
after complete success, and explicitly permits the image to unload when that
discardable staging transaction fails. Arbitrary external registrars retain
the conservative image quarantine when their accepted callbacks cannot be
rolled back, preventing stale code pointers.

Dedicated negative images and tests cover missing images and entry points,
host/registrar ABI mismatch, initialization exceptions, ignored duplicate
registration, construction status/null/exception failures with pending-handle
rollback, escaped process callbacks, throwing destructors, and image unload
after containment. The compiler fixture now executes portable preprocessing/
compile and link failures and requires exact `FSIM-SC-C007` diagnostics and
captured compiler output; existing missing compiler, option, dependency,
publication, and changed-during-compile cases remain intact. The exact eight-
worker Debug build succeeded; loader, compiler, application, diagnostics
catalog, and source budget passed 5/5 in 56.05 seconds, with the loader at
0.00, compiler at 2.42, and application at 53.41 seconds. The plug-in loader,
hierarchy, loader test, negative image, empty image, and compiler test remain
at 352, 941, 558, 149, 6, and 1,128 lines. Task 7 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this task
boundary.

Task 7 versions specialization provenance as
`fsim-specialization-provenance-v4` and composes the exact SystemC plug-in
compile key with every stable factory, typed construction, instance-path,
port/event/channel/signal/export, process, and debugger-visible object mapping.
Transient image addresses, callback pointers, and native handles stay out of
persistent objects: each fresh interpreter binds those values through its
owned hierarchy registry, while LLVM O0/O2 and debug modules reuse native code
only when the stable common-runtime mapping is unchanged. Identical builds
retain the same specialization keys; selecting a compatible alternative
factory or a native-child hierarchy changes them.

The SystemC scheduling matrix proves interpreter semantics, cold/warm LLVM O0
and O2 reuse, and cold/warm debug O0 reuse. Editing the loaded plug-in source
now produces native-cache misses and stores rather than the formerly accepted
hits, preventing reuse across changed callbacks or hierarchy construction. The
exact eight-worker Debug build succeeded; the application passed in 54.39
seconds, and plug-in, compiler, diagnostics catalog, and source budget passed
4/4 in 2.48 seconds. The identity implementation, SystemC integration test,
and scheduling matrix remain at 828, 1,886, and 352 lines. Task 8 is current;
no sanitizer, Release, full regression, commit, push, or CI inspection ran at
this task boundary.

Task 8 hardens the MSVC-compatible source-compiler contract without changing
the portable direct-argv path. Compile commands select UTF-8 C++20 diagnostics,
the host's exact CRT and Debug/Release optimization/debug mode, unique object
and source-PDB paths, and full paths in captured diagnostics. The x86-64 DLL
link uses explicit nonincremental PE output, link-PDB, import-library, and
export paths; raw options cannot override those outputs, machine type, or
incremental policy. The fetched Windows Boost.Context path now rejects
non-64-bit or known non-x86-64 targets before selecting its three PE/MASM
fcontext sources.

Windows process launch validates inherited-handle-list sizing, passes only
stdin and the merged diagnostic pipe, quotes a resolved executable separately
from its mutable command line, uses a UTF-16 response file before the process
limit, and distinguishes launch, pipe-read, wait, and exit-status failures.
Windows-only execution forces a greater-than-command-line-limit argument set
through dependency and real compilation in a path containing spaces, then
requires cold/warm success and response-file cleanup. DLL loading now resolves
the absolute image and searches its directory plus safe system defaults,
independent of the caller's current directory. Suspended SystemC threads are
drained through bounded repeated stop resumes before callbacks or images can
be destroyed; the sample thread deliberately yields three times to prove that
ordering on every fiber-enabled host.

The exact eight-worker Debug build succeeded; plug-in, compiler, application,
diagnostics catalog, and source budget passed 5/5 in 56.47 seconds, with the
application at 53.84 and compiler at 2.43 seconds. The process, compiler-plan,
thread-callback, dynamic-loader, compiler-test, and sample-plug-in sources
remain at 510, 1,333, 1,730, 185, 1,181, and 348 lines. Task 9 is current; no
sanitizer, Release, full regression, commit, push, or CI inspection ran at this
task boundary.

Task 9 adds a dedicated 231-line `fsim.systemc.matrix` that compiles a copied
real plug-in through the public source compiler, proves cold/warm publication,
loads factories and roots, suspends a thread where the fiber backend is
available, edits the still-loaded image's source into a distinct artifact,
repairs a corrupt DLL/shared object, races three concurrent rebuild callers,
proves old/new image residency and unload, and requires exact compile-failure
diagnostics. It runs in every configured Linux and Windows Debug/Release job.

The merged application host now exposes the existing integration/scheduling
coverage as `fsim.application.systemc_matrix`, labelled for SystemC, compiler,
cache, lifecycle, threads, LLVM, debug, and portability. This retains one
physical test executable while giving Windows MSVC and MSVC/clang-cl LLVM O0,
O2, debug, interpreter, edit, trace, and thread coverage an independently
selectable process. Nonmerged builds retain the original core invocation. The
named application matrix passed in 44.67 seconds; the compact matrix passed in
0.70 seconds; diagnostics catalog, source budget, and the newly shortened core
application passed 4/4 in 10.06 seconds with core at 9.15 seconds. The new
matrix and selector remain at 231 and 15 lines, and `tests/CMakeLists.txt`
remains at 1,765 lines. Task 10 is current; no sanitizer, Release, full
regression, commit, push, or CI inspection ran at this task boundary.

Task 10 closes the compiler/cache/portability batch. The release authority,
SystemC subset, and diagnostics reference now describe canonical compiler and
dependency fingerprints, transactional artifact publication, transactional
registration, retained executable ownership, specialization provenance, exact
failure containment, and the Windows compiler/process/DLL/fiber contract. The
diagnostics catalog covers all 1,622 production codes, and all 402 authored
C/C++ sources pass the 2,000-line gate. The LLVM-disabled ASan/UBSan suite
passed 77/77 in 437.42 seconds with leak detection disabled only because the
local sandbox denies LeakSanitizer's ptrace operation; strict ASan string
checks and halt-on-error UBSan remained enabled. The named SystemC matrix,
containers, and scoped locals took 131.90, 212.10, and 0.51 seconds.

The exact LLVM 22.1.8 warnings-as-errors Debug suite passed 80/80 in 230.94
seconds, with the named SystemC matrix at 45.81 seconds, containers at 111.06,
and scoped locals at 0.83. Release passed 80/80 in 206.43 seconds, with the
named SystemC matrix at 43.36 seconds, containers at 92.83, and scoped locals
at 0.83. Batch 125 closes as one accumulated checkpoint and has no GitHub
Actions inspection because it is not a tenth-batch boundary.

### Batch 126 — Typed HIR and DesignIR boundaries — Complete

The current ten implementation tasks are:

1. **Complete.** Audit every VHDL and SystemVerilog parse-tree dependency in
   analysis, elaboration, specialization, execution, diagnostics, cache, and
   debugger paths; define the exact typed-HIR and elaborated-DesignIR boundary
   gaps, identity requirements, and migration order.
2. **Complete.** Introduce shared stable semantic identities, owning source-file
   and expansion provenance, exact source spans, type/value references, and
   deterministic traversal contracts without retaining parser-owned storage.
3. **Complete.** Complete typed VHDL HIR for design units, declarations, scopes,
   names, overload sets, subtypes, constraints, aliases, attributes, generics,
   ports, components, packages, configurations, and generated declarations.
4. **Complete.** Complete typed VHDL HIR for expressions, aggregates, sequential
   and concurrent statements, call associations, waits, assertions, files,
   protected/access operations, and waveform transactions.
5. **Complete.** Complete typed SystemVerilog HIR for compilation units, modules,
   packages, interfaces, declarations, scopes, nets/variables, parameters,
   types, ports/modports, callables, classes permitted by v1, and generates.
6. **Complete.** Complete typed SystemVerilog HIR for expressions, selections,
   assignment patterns, statements, processes, timing/event controls, forks,
   assertions, system tasks, strings, files, and containers.
7. **Complete.** Complete elaborated DesignIR for hierarchy, specializations,
   objects, drivers, ports/exports, callables, processes, conversions,
   sensitivities, transactions, and mixed-language/SystemC boundaries using
   stable semantic identities only.
8. **Complete.** Migrate lowering, interpreter, LLVM, cache/provenance,
   diagnostics, debugger, VCD, and API consumers to the explicit boundaries;
   prove no downstream consumer depends on parse-tree addresses or lifetimes.
9. **Complete.** Add a combined VHDL/SystemVerilog/mixed/SystemC boundary matrix
   covering positive and negative legality, stable identities, source and
   macro provenance, cold/warm/edit cache behavior, interpreter/O0/O2/debug
   agreement, serialization-order independence, and Windows portability.
10. **Complete.** Update matrix/architecture/diagnostics/docs, pass sanitizer,
    source/catalog and full Debug/Release gates, then create and push the single
    Batch 126 checkpoint. This is not a mandatory CI-inspection boundary.

Batch status is **complete**. This exact ten-task list remains the retained
official Batch 126 record. Tasks 1 through 9 used one accumulated dirty
worktree with focused eight-worker Debug builds and tests; Task 10 owns the
sanitizer, full regressions, documentation, commit, and push gate. GitHub
builds use parallelism four, and Batch 126 does not require a
non-documentation CI inspection.

Task 1 fixes the migration plan to the live boundaries. The common 1,588-line
`frontend::ParsedDesign` is a value-owned parser result, partially typed HIR,
and mutable semantic workspace at once: project build selection rewrites delay
metadata, and elaboration copies and mutates `DesignUnit` values for package,
type, callable, generate, and per-occurrence specialization before lowering
directly to SimIR. Its 68 source-span occurrences retain logical/physical
locations, but ordinary nodes do not retain the preprocessor token's macro
expansion stack. Declarations, scopes, types, expressions, and statements have
no stable semantic IDs; the elaborator internal surface has 112 explicit
frontend pointer/reference declarations.

The coupling inventory finds 2,546 `frontend::` uses across 80 elaboration
files and 145 across eight application files. Compiler and runtime
implementation files have zero such uses and remain the protected downstream
boundary. `ElaboratedDesign` already owns dense signal/process/string/
container/protected/specialization/SystemC mappings, but its public header has
47 frontend references and no explicit library/unit/scope/instance/port/
driver/type/source identity tables. The architecture now records the exact
additive migration: shared semantic IDs and owning provenance first, separate
VHDL and SystemVerilog HIR next, explicit DesignIR after that, then consumer
migration and compatibility-adapter removal.

Task 2 adds the pointer-free `fsim::semantic::Model` and strong dense IDs for
the complete planned semantic/DesignIR identity space. Its owning tables now
intern physical files with exact content digests, logical/physical source
spans, nested expansion chains, and generated/specialized origin chains, then
retain units, root scopes, declared types, values, and instances through IDs.
`check_project` builds the model only after canonical source merging and
standard-library injection. Root/dependency sources preserve manifest and
first-use order; units preserve canonical source order; declarations sort by
physical source offset with stable tie breakers; VHDL and SV name keys retain
their distinct case rules. A production application test proves IDs repeat on
a second check and that all semantic names and spans survive clearing the
parser tree. The exact eight-worker LLVM 22 Debug build passed, followed by
semantic/frontend/application focused tests (3/3 in 9.20 seconds), the
expanded semantic/application ownership gate (2/2 in 9.41 seconds), and
catalog/source gates (2/2 in 0.12 seconds). Task 3 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection runs at this task
boundary.

Task 3 adds the separate owning `semantic::vhdl::Hir` and extends the common
model with stable declaration, expression, and statement identity tables.
VHDL units now retain contexts and entity/architecture relations; declarations
cover every current generic family, ports, objects/files, aliases, callable
profiles and nested declarations, generic templates/instances, package
instances, components, configurations, and recursive generated declarations.
Explicit scopes cover callables, components, protected types, and generate
regions, while canonical symbol groups retain overload sets without parser
pointers. Type definitions distinguish aliases/subtypes, enumerations, arrays,
records, access, files, protected declarations/bodies, physical types, all
constraints, resolution names, fields/elements/units, and applicable predefined
attributes. Stable association records preserve configuration, generic, port,
and block maps; expression-bearing fields already point to shared expression
IDs for Task 4 to fill.

The direct type-generic HIR test proves stable unit/scope/type identities,
record fields, repeated-check determinism, and survival after clearing
`ParsedDesign`. The exact eight-worker LLVM 22 Debug build passed. The complete
VHDL application slice passed 26/26 in 2.76 seconds, and the final semantic,
source/catalog, generic-profile, advanced-type, and attribute focus passed
10/10 in 0.62 seconds. Task 4 is current; no sanitizer, Release, full
regression, commit, push, or CI inspection runs at this task boundary.

Task 4 fills the VHDL HIR's stable expression, statement, and new process IDs
with owning executable payloads. Recursive expressions retain names and
overload candidates, literal/operator/call/selection kinds, named actuals,
decoded strings, nominal identity, and positional, named, range, and `others`
aggregate associations. Callable and generic-template bodies, protected
methods, explicit/generated processes, concurrent statements, and lexical
block variables now use explicit scopes and IDs. Statement records cover
signal/variable assignments, conditions/selections, loops and loop control,
procedure associations, returns, waits, assertions/reports, blocks, and null
statements. Ordered waveforms retain every value/delay, inertial/transport
mode, rejection limit, disconnect/`unaffected` state, and exact source span;
resolved call/name records carry file, protected, access, and attribute
operations without retaining frontend nodes.

Direct record-aggregate checks prove named, `others`, and positional
associations. The projected-waveform check proves three-element transactions,
delay magnitudes, transport mode, `unaffected`, waits, dense expression/
statement/process identity coverage, and parser-lifetime independence. The
exact eight-worker LLVM 22 Debug build passed, and the final semantic,
frontend, catalog/source, and complete VHDL application focus passed 30/30 in
2.76 seconds. Task 5 is current; no sanitizer, Release, full regression,
commit, push, or CI inspection runs at this task boundary.

Task 5 adds the separate owning `semantic::sv::Hir`. Module, package, and
interface units retain their compilation context, imports/exports, declarations,
instances, generated regions, process identities, and concurrent-statement
slots through shared stable IDs. Declarations cover value/type parameters,
local parameters, typedefs, ports, nets, variables, functions, tasks, modports,
enumeration literals, callable formals/locals/bodies, interface types, and exact
automatic/static lifetime. The v1 boundary explicitly excludes classes because
unsupported class syntax is rejected before HIR construction.

Packed integral, enum, packed/unpacked structure and union, dynamic-array,
bounded-queue, associative-array, fixed-array, string, alias, and type-parameter
forms retain signedness, packed/unpacked dimensions, queue bounds, associative
index types, members/offsets, enum values, and source provenance without parser
pointers. A direct fast HIR application proves compilation context, modports,
container types, enum/struct metadata, imports, instances, generates,
deterministic IDs, and survival after clearing `ParsedDesign`. The exact
eight-worker LLVM 22 Debug build passed. Twelve representative non-container
SystemVerilog applications passed 12/12 in 11.60 seconds and the heavyweight
container matrix passed once in 112.93 seconds. The final semantic/frontend,
catalog/source, interface, type-parameter, aggregate, and direct HIR focus
passed 8/8 in 1.63 seconds. Task 6 is current; no sanitizer, Release, full
regression, commit, push, or CI inspection runs at this task boundary.

Task 6 fills every shared SystemVerilog expression, statement, and process ID
with parser-independent owning payload. Expressions distinguish names,
literals, unary/binary/update operations, calls/casts, index and slice
selections, concatenation/replication, and keyed/defaulted assignment patterns;
decoded strings, nominal typing, named or positional call associations, and
explicit empty named actuals retain their source spans. Initial/final and all
four always-process forms own declarations, sensitivities, and statement roots,
while callable bodies and selection-generate alternatives use the same records.

Statements retain blocking/nonblocking/continuous assignment, captured
compound/prefix/postfix update form, intra-assignment delay/event control,
force/release, conditions and qualified cases, runtime/static loops, task
associations, returns, delay/event/condition waits, event triggers, fork/join
and wait/disable-fork control, assertions and action presence, display/strobe/
monitor formatting, file handles and operations, memory radix/direction/bounds,
container calls, finish/pause, and lexical block declarations/scopes. A direct
fixture proves complete dense identity coverage and survival after clearing
`ParsedDesign`.

The exact eight-worker LLVM 22 Debug build passed. The representative
expression/event/fork/assertion/procedural/task/string/file/interface/aggregate
application gate passed 16/16 in 8.40 seconds; the heavyweight container matrix
passed once in 112.42 seconds. After final association and generate-alternative
hardening, the semantic/frontend/catalog/source and executable SystemVerilog
focus passed 10/10 in 1.24 seconds. Task 7 is current; no sanitizer, Release,
full regression, commit, push, or CI inspection runs at this task boundary.

Task 7 adds the owning `semantic::design::DesignIr` and retains its semantic
model in `BuiltProject`. Dense stable IDs cover realized specializations,
instance occurrences, objects, process occurrences, sensitivities,
transactions, conversions, and mixed/external boundaries; shared stable port
and driver IDs complete the cross-record graph. Internal validation checks
every relationship, while model-aware validation checks every unit, scope,
source instance, declaration, type/value, source-process, span, and origin ID.
Legacy runtime indices and native SystemC handles remain explicitly named
adapter locators rather than identities.

The builder materializes HDL hierarchy and specialization parameters,
callables, packed/string/container/protected objects and aliases, ports/exports,
process sensitivities, driver regions and transactions, all cross-language
conversion profiles, and SystemC modules, ports, events, channels, signals,
exports, processes, construction identities, and writable-export policy. A
direct two-level SystemVerilog hierarchy proves parentage, ports, objects,
processes, sensitivities, drivers, and transactions. The existing mixed matrix
proves all 28 conversions have owning formal/actual IDs and boundary records;
the production SystemC integration proves plug-in objects/exports/processes and
their semantic owner survive the build handoff.

The exact eight-worker LLVM 22 Debug build passed. The final direct
SystemVerilog, VHDL projected, mixed-conversion, SystemC/core application,
semantic, and source focus passed 6/6 in 9.41 seconds; the direct hierarchy took
0.02 seconds, mixed conversion 0.80, VHDL 0.19, and the SystemC/core host 9.40.
Task 8 is current; no sanitizer, Release, full regression, commit, push, or CI
inspection runs at this task boundary.

Task 8 moves the checked parser workspace into one temporary owning lowering
adapter, applies delay selection/time normalization there, and regenerates the
semantic model plus both typed HIRs from that exact input. The adapter is
destroyed immediately after legacy elaboration copies diagnostics and SimIR;
DesignIR construction and every durable consumer therefore run after the
original parser owner is empty. A mandatory runtime-projection validator now
cross-checks every HDL specialization, dense signal and alias, container alias,
process, mixed conversion, SystemC instance, and named SystemC object/process
before a build can escape the boundary.

LLVM module grouping, interpreter executor installation, specialization cache
schema `fsim-specialization-provenance-v5-designir`, debugger scopes/objects,
VCD declarations, Tcl, and the C API now take identity, hierarchy, paths, and
sources from DesignIR. The remaining SimIR-rich payload is accessed only
through the explicitly named `Simulation::runtime_adapter()` and never supplies
identity. Stable SystemC instance/object/boundary mappings enter cache identity;
transient native handles do not. Compiler, runtime, and API implementations
retain zero frontend references.

The exact eight-worker LLVM 22 Debug tree rebuilt cleanly. The isolated SystemC
matrix passed in 45.46 seconds. The final core/cache, direct HIR, mixed/VCD/
debugger, Tcl, API, and scoped-locals focus passed 6/6 in 9.46 seconds; API took
1.22 seconds and scoped locals 0.86.

Task 9 adds selector-hosted `fsim.application.typed_boundaries`, one combined
SystemVerilog-to-VHDL-to-SystemC execution graph with a width conversion and
native method boundary. Parsed source spans now own token macro-expansion
ancestry and intern its exact parent chain into the semantic model. SystemC
translation-unit roots likewise enter the owning semantic source table with
normalized generic paths and content digests instead of existing only in the
host-compiler cache key. The matrix requires positive construction plus a
stable negative missing-factory diagnostic, valid DesignIR relationships,
stable record IDs across rebuilds, canonical forward/reverse serialization,
macro and all-language source provenance, portable path spelling, normalized
VCD, and identical interpreter, compiled O0/O2, and debug results. It proves
cold/warm native and project-cache reuse, then edits the macro source and
requires changed behavior, specialization identity, and native misses.

The exact eight-worker LLVM 22 Debug tree rebuilt cleanly. The final semantic,
frontend, core application, SystemC matrix, scoped-locals, mixed-conversion,
and typed-boundary focus passed 7/7 in 46.96 seconds. Typed boundaries took
4.11 seconds, the SystemC matrix 46.96, mixed conversions 0.79, and scoped
locals 0.95. Task 10 is current; no Release, full regression, commit, push, or
CI inspection has run at this task boundary.

Task 10 closes the typed-boundary batch and repairs two defects exposed by its
full sanitizer gate. DesignIR now projects every debug-visible container path,
including top-level and whole-port aliases, as one stable container object with
the applicable port metadata; the container application asserts exact coverage
of the runtime path table. C API source lookup now returns views into the
owning semantic source table instead of a temporary copied path, eliminating a
sanitizer-detected use-after-free. Internal DesignIR validity and incomplete
runtime projection also produce distinct invariant failures.

The diagnostic catalog covers all 1,622 production codes, and the empty-
allowlist source gate covers 419 authored files at or below 2,000 lines. The
LLVM-disabled ASan/UBSan suite passed 80/80 in 570.42 seconds, including
containers in 281.73 seconds, typed boundaries in 14.23, API in 3.00, and
scoped locals in 0.62. The exact LLVM 22.1.8 warnings-as-errors Debug suite
passed 83/83 in 254.55 seconds, including containers in 132.56 seconds,
SystemC matrix in 45.68, typed boundaries in 4.10, API in 1.19, and scoped
locals in 0.83. Release passed 83/83 in 216.93 seconds, including containers
in 100.01 seconds, SystemC matrix in 43.88, typed boundaries in 3.56, API in
1.17, and scoped locals in 0.85. Batch 126 closes as one accumulated
checkpoint and has no GitHub Actions inspection because it is not a tenth-
batch boundary.

### Batch 127 — Language-wide legality closure — Complete

The current ten implementation tasks are:

1. **Complete.** Inventory every required v1 VHDL, Verilog/SystemVerilog,
   mixed-language, and SystemC construct against the parser, typed HIR,
   analysis, elaboration, execution, and diagnostic paths; identify every
   silently accepted, discarded, parser-only, or under-diagnosed case.
2. **Complete.** Add common legality-audit infrastructure and stable diagnostics
   that require every accepted required construct to reach an owning semantic
   record and every unsupported or illegal construct to fail explicitly.
3. **Complete.** Close Verilog/SystemVerilog declaration, type, parameter, port,
   interface/package, generate, and specialization legality gaps with exact
   positive and negative evidence.
4. **Complete.** Close Verilog/SystemVerilog expression, lvalue, callable,
   process, statement, timing, event, assertion, system-task, file, string,
   container, and memory legality gaps.
5. **Complete.** Close VHDL library/unit, context, declaration, subtype/type,
   name, overload, generic, port, component, configuration, and generate
   legality gaps with exact positive and negative evidence.
6. **Complete.** Close VHDL expression, aggregate, callable, sequential,
   concurrent, wait, assertion/report, file, access, protected, physical, and
   waveform legality gaps.
7. **Complete.** Close mixed-language binding, type conversion, construction
   parameter, driver/resolution, scheduling, hierarchy, and unsupported-
   boundary legality gaps.
8. **Complete.** Close SystemC facade, native ABI, named-object, port/export,
   process/sensitivity, event/channel, lifecycle, plug-in, and unsupported-
   subset legality gaps.
9. **Complete.** Add one language-wide positive/negative legality matrix proving
   no required construct is silently discarded or remains parser-only, with
   stable diagnostics, cache/provenance, interpreter/O0/O2/debug, VCD, source,
   and portable-path evidence.
10. **Complete.** Update matrix/architecture/diagnostics/docs, pass sanitizer,
    source/catalog and full Debug/Release gates, then create and push the single
    Batch 127 checkpoint. This is not a mandatory CI-inspection boundary.

Batch status is **complete**. Keep this exact completed ten-task list in both
the official plan and this handoff while Batch 128 is current. Tasks 1 through
9 used one accumulated dirty worktree with focused eight-worker Debug builds
and tests; Task 10 owns the single sanitizer, full-regression, documentation,
commit, and push gate. GitHub builds use parallelism four, and Batch 127 does
not require a non-documentation CI inspection.

Task 1 records the complete closure set in
[`docs/v1-legality-audit.md`](v1-legality-audit.md). The initial matrix had
1,080 required rows: 791 `execute` rows and 289 rows below execution status. The
closure set is exactly 102 VHDL, 173 Verilog/SystemVerilog, one mixed-language,
one SystemC, and 12 common/release-contract rows. Within it, 106 rows already
have all four evidence cells and require coverage verification plus status
correction; the non-`execute` cells contain 2 P+, 16 P-, 15 E, and 175 R gaps.
An additional 109 `execute` rows retain at least one dash, bringing the complete
required-row gap inventory to 292 rows and 8 P+, 120 P-, 28 E, and 176 R empty
cells. The audit lists every affected ID, isolates all 13 parser-only rows,
flags the undocumented `metadata` status on `SC-024`, defines owning-record and targeted-
diagnostic closure rules, and routes each row to Tasks 2 through 9. The initial
discard-site scan confirms the explicitly named VHDL `Statement ignored`
temporary follows a targeted misplaced-delay diagnostic; all other parse-result
discard sites remain in the per-language classification queues.

Task 2 is focused-complete. The new `fsim.v1-legality-audit` CTest parses all
required feature-matrix rows without treating escaped pipes as columns, pins
the 1,080-row status distribution and 292-row evidence-gap baseline, rejects
duplicate IDs and undocumented statuses, requires every open row in this
audit, and verifies every path-like evidence link. Applying the gate repaired
stale elaboration links, a missing fork fixture link, and the duplicate
`SV-100` identity now assigned uniquely to `SV-671`. The pointer-free semantic
model now validates every dense ID and owned relationship before application
checking and DesignIR projection; invalid ownership fails with cataloged
`FSIM-SEM-0001`. The exact eight-worker Debug build succeeded, and semantic,
SystemVerilog HIR, legality/matrix, diagnostics/source, typed-boundary, and
scoped-locals tests passed 9/9 in 5.63 seconds, with typed boundaries in 4.18
seconds and scoped locals in 0.94. Task 3 is current; no sanitizer, Release,
full regression, commit, push, or CI inspection is due at this task boundary.

Task 3 is focused-complete. The legality gate now names and enforces 53 exact
declaration/type rows covering non-ANSI Verilog ports; integral, type, and
string parameters; static, dynamic, and sliced hierarchy ports; interfaces,
modports, and package re-exports; preprocessing/directive state; generated
declarations; and aggregate/multidimensional types. All 53 are `execute` with
complete P+, P-, E, and direct R evidence. Duplicate non-ANSI declarations now
have an explicit `FSIM-SV-SEM-004` regression, and the Verilog-2005 non-ANSI
`output reg` path executes through the interpreter. The required matrix now
has 844 executable and 236 open rows; the SystemVerilog queue falls from 173
to 120 and the all-language evidence-gap queue from 292 to 261. The exact
eight-worker Debug build succeeded. The 13-test frontend, elaboration,
parameter, type, string, port/container, interface/package,
preprocessor/generate, aggregate, catalog/source, HIR, and legality gate passed
in 132.61 seconds; the intentionally merged container application accounted
for 125.89 seconds. Scoped locals passed separately in 0.81 seconds. Task 4 is
current; no sanitizer, Release, full regression, commit, push, or CI inspection
is due at this task boundary.

Task 4 is focused-complete. It promotes the remaining 120 SystemVerilog
expression, callable, process, timing, assertion, file/string, container,
memory, and statement rows and supplies the missing evidence for 76 rows that
were already executable. The legality gate now requires every one of the 671
required SystemVerilog rows to remain `execute` with nonempty P+, P-, E, and R
evidence. The all-language matrix stands at 964 executable and 116 open rows,
with its evidence-gap queue reduced to 98 and no SystemVerilog entry remaining.
The 24-test Debug focus spanning frontend/elaboration, core and expression
applications, scheduling/control, assertions, callables, strings/files,
containers, catalog/source, and the legality gate passed in 168.92 seconds.
The merged container application took 127.33 seconds and scoped locals 0.87.
Task 5 is current; no sanitizer, Release, full regression, commit, push, or CI
inspection is due at this task boundary.

Task 5 is focused-complete. It closes 97 non-execute unit, declaration, type,
name, generic, component, configuration, generate, and binding rows plus four
pre-existing negative-evidence gaps. The legality gate now protects 242 of 252
VHDL rows as executable with complete evidence and leaves exactly ten
expression/callable rows to Task 6. The complete matrix now has 1,061
executable and 19 open rows, with 39 rows retaining any evidence gap. The
17-test Debug focus covering frontend/elaboration, all generic families,
overloads, configurations, analysis order, components, reviewed packages,
catalog/source, core execution, and legality passed in 16.52 seconds; scoped
locals took 0.85. Task 6 is current; no sanitizer, Release, full regression,
commit, push, or CI inspection is due at this task boundary.

Task 6 is focused-complete. Its exact ten-row expression/callable remainder is
now executable with complete evidence, and the legality gate requires all 252
VHDL rows to remain fully closed. The all-language matrix reaches 1,066
executable rows with 14 open statuses and 31 rows retaining any evidence gap.
The 34-test Debug focus covered the full VHDL application surface plus
frontend/elaboration, expressions, resolution, catalog/source, and legality;
all passed in 15.98 seconds. VHDL-labeled applications accounted for 12.70
seconds, arrays 2.18, overloads 1.90, and scoped locals 0.86. Task 7 is
current; no sanitizer, Release, full regression, commit, push, or CI inspection
is due at this task boundary.

Task 7 is focused-complete. Parser-only manifest binding plus the three
pre-existing mixed-hierarchy negative gaps are closed, and the gate now
requires all 16 mixed-language rows to remain executable with complete
evidence. The complete matrix reaches 1,067 executable rows and 27 rows with
any evidence gap. The 12-test Debug focus spanning project/frontend,
elaboration, the compact conversion matrix, recursive mixed execution,
resolution, typed boundaries, catalog/source, legality, and scoped locals
passed in 16.41 seconds; typed boundaries took 4.02, core mixed execution 9.62,
and scoped locals 0.84. Task 8 is current; no sanitizer, Release, full
regression, commit, push, or CI inspection is due at this task boundary.

Task 8 is focused-complete. `SC-024` now uses the documented `execute` status,
the earlier facade/signal evidence gaps are filled, and the legality gate
requires all 28 SystemC rows to remain executable with complete evidence. The
matrix reaches 1,068 executable rows with only the 12 common/release statuses
open and 24 rows retaining any gap. The 13-test Debug focus covering facade,
strict ABI, loader/compiler matrices, lifecycle/thread execution, datatypes,
typed boundaries, catalog/source, legality, and scoped locals passed in 68.97
seconds. The SystemC application matrix took 47.89 seconds, typed boundaries
4.00, and scoped locals 0.85.

Task 9 is focused-complete. The release-authority gate now requires all 1,080
required rows to be `execute`, every P+, P-, E, and R cell to be populated,
all path-like evidence links to resolve, all IDs to be unique, and the exact
language row counts to remain stable. The final 12 common/release status rows
and 18 existing common evidence-gap rows are closed. The 18-test Debug focus
covering semantic/project/cache, runtime/LLVM/C ABI, API/Tcl, VHDL/mixed/
legality matrices, core/SystemC execution, typed boundaries, catalog/source,
and scoped locals passed in 64.83 seconds; the SystemC application matrix took
45.85, typed boundaries 3.90, and scoped locals 0.83. Task 10 is current; it
owns the single sanitizer, full Debug/Release, documentation, commit, and push
closure. Batch 127 is not a mandatory CI-inspection boundary.

Task 10 is complete. The release authority now contains 1,080 required rows,
all `execute` with complete positive, negative, elaboration, and runtime
evidence. The legality gate also validates unique IDs, exact language/status
counts, documented states, audit membership, and every path-like evidence
link. The semantic ownership validator rejects invalid dense IDs and
relationships with `FSIM-SEM-0001`; the direct Verilog-2005 non-ANSI path has
stable duplicate-port diagnostics and interpreter execution. The
LLVM-disabled ASan/UBSan suite passed 81/81 in 541.69 seconds with leak
detection disabled for the managed ptrace environment; containers took
277.62 seconds, SystemC matrix 139.37, typed boundaries 13.92, and scoped
locals 0.60. Exact LLVM 22.1.8 warnings-as-errors Debug passed 84/84 in
245.89 seconds, with containers at 124.34, SystemC matrix 47.16, typed
boundaries 3.87, and scoped locals 0.85. Release passed 84/84 in 205.75
seconds, with containers at 94.82, SystemC matrix 41.67, typed boundaries
3.51, and scoped locals 0.84. The diagnostic catalog covers 1,623 production
codes, all 419 authored sources pass the 2,000-line gate, and `git diff
--check` is clean. This is the single accumulated Batch 127 commit/push
checkpoint; no CI inspection is due at this non-boundary batch.

### Batch 128 — License-reviewed conformance differentials — Complete

The current ten implementation tasks are:

1. **Complete.** Inventory the checked-in language, runtime, API, debugger,
   and tool tests against public Apache-2.0-compatible conformance sources;
   record license, provenance, bounded expectation, and uncovered semantic
   families without importing incompatible test text.
2. **Complete.** Add license-reviewed Verilog/SystemVerilog preprocessing,
   declaration, type, parameter, package/interface, and generate conformance
   cases with exact positive and negative frontend/elaboration evidence.
3. **Complete.** Add license-reviewed Verilog/SystemVerilog expression,
   statement, process, callable, timing, assertion, file/string/container, and
   memory conformance cases with direct runtime evidence.
4. **Complete.** Add license-reviewed VHDL library/unit, context, declaration,
   type, generic, component, configuration, generate, and analysis-order
   conformance cases.
5. **Complete.** Add license-reviewed VHDL expression, aggregate, callable,
   sequential/concurrent, wait/report, file, access/protected/physical,
   transaction, and reviewed-package conformance cases.
6. **Complete.** Add bidirectional mixed-language construction, conversion,
   ownership, resolution, timing, hierarchy, and failure conformance cases.
7. **Complete.** Add SystemC facade, ABI, hierarchy, datatype, process/event/
   channel, lifecycle/thread, plug-in/compiler/cache, and unsupported-subset
   conformance cases.
8. **Complete.** Add common scheduler/SimIR/LLVM/cache, C API, debugger, VCD,
   project/CLI, Tcl, diagnostics, source/provenance, and failure-containment
   conformance cases.
9. **Complete.** Prove the complete conformance corpus through interpreter, LLVM
   O0/O2, cold/warm/edit cache, debugger, callbacks, normalized VCD, source
   mapping, and portable-path differentials with an exact coverage gate.
10. **Complete.** Update license inventories, matrix/architecture/diagnostics/
    docs, pass sanitizer, source/catalog and full Debug/Release gates, then
    create and push the single Batch 128 checkpoint. This is not a mandatory
    CI-inspection boundary.

Batch status is **complete**. This exact ten-task list remains the retained
Batch 128 record in both the official plan and this handoff. Tasks 1 through 9
used one accumulated dirty worktree with focused eight-worker Debug builds and
tests; Task 10 owned the sanitizer, full regressions, documentation, commit,
and push gate. GitHub builds use parallelism four, and Batch 128 did not
require a non-documentation CI inspection.

Task 1 is focused-complete. The new
[`docs/v1-conformance-audit.md`](v1-conformance-audit.md) records 184 tracked
test-tree files, including 179 Apache-2.0-authored C/C++/CMake test and control
files, 84 named Debug CTests, all 1,080 executable release-matrix rows, and the
sole reviewed third-party IEEE 1076-2019 package tree. It pins ten original or
permissively licensed public-source identities, explicitly excludes six
copyleft, ambiguous, proprietary, or unlicensed source classes from direct
test-text import, and routes eight exact semantic/provenance queues to Tasks 2
through 9. No upstream test text was imported. The new
`fsim.v1-conformance-audit` gate requires every authored test/control file to
retain an Apache-2.0 SPDX notice, protects every reviewed/excluded source and
queue ID plus pinned commit identity, and rejects any unreviewed third-party
root. The exact eight-worker Debug tree reconfigured and required no
compilation; the conformance, legality, IEEE-package, source, catalog, and
scoped-locals focus passed 6/6 in 0.83 seconds, with scoped locals in 0.83.
Task 2 is current; no sanitizer, Release, full regression, commit, push, or CI
inspection is due at this task boundary.

Task 2 is focused-complete. Two independently authored suites add 17 stable
`CF-SV-*` expectation markers cross-referenced to the pinned permissive
sv-tests, Surelog, and slang sources without copying upstream test text. The
frontend pairs cover defaulted/token-pasted macros, `macromodule`, escaped
keyword identifiers, typed package enums and aliases, value/type parameters,
parameterized interfaces/modports, static generate HIR, macro arity, reserved
names, and matching module/interface closing labels. The elaboration pairs
prove parameterized macro-module instances, package/interface binding, exact
generated paths and widths, plus nonstatic-generate, required-type-parameter,
ambiguous-package-type, and wrong-interface-type diagnostics. This exposed and
fixed two real gaps: `macromodule` now enters the ordinary module semantic
path, and mismatched module/interface end names fail with cataloged
`FSIM-SV-SEM-129`. The complete Debug tree rebuilt with eight workers. The
frontend, elaboration, SystemVerilog HIR, parameter, type-parameter,
interface, preprocessor/generate, aggregate, conformance/legality,
catalog/source, and scoped-locals focus passed 13/13 in 2.29 seconds; scoped
locals took 0.91 seconds. Task 3 is current; no sanitizer, Release, full
regression, commit, push, or CI inspection is due at this task boundary.

Task 3 is focused-complete. The independently authored merged SystemVerilog
runtime application adds 11 stable `CF-SV-*` expectation markers
cross-referenced to the pinned permissive sv-tests, Surelog, and slang sources
without copying upstream test text. One compact vertical slice proves
short-circuit and wildcard expressions, loop control, automatic function/task
copy and suspension behavior, named-event and nonblocking timing, immediate
assertions, mutable string formatting, exact text-file I/O, read/write memory,
and dynamic/queue/associative container execution. It compares interpreter
with LLVM O0/O2 cold and warm runs through exact terminal time, signal values,
output bytes, memory bytes, specialization identity, compilation counts, and
cache telemetry. The complete Debug tree rebuilt with eight workers. The
frontend/elaboration, audit, catalog/source, runtime, expressions, event/fork,
assertion, time, callable, string, file, container, scoped-local, and new
conformance focus passed 20/20 in 155.07 seconds; the new case took 4.10
seconds, the heavyweight container regression 124.33 seconds, and scoped
locals 0.86 seconds. Task 4 is current; no sanitizer, Release, full regression,
commit, push, or CI inspection is due at this task boundary.

Task 4 is focused-complete. The compact VHDL structural and analysis-order
application now carries 17 stable `CF-VHDL-*` expectation markers
cross-referenced to the pinned Apache-2.0 IEEE package source and UVVM without
copying upstream test text. It proves package, context, declaration, subtype,
generic, component, configuration, and generate acceptance plus generated
component elaboration, deterministic valid source-unit order, and all eight
exact package-body, context/use, architecture/configuration, entity binding,
configuration binding, and direct-configuration order failures. The complete
Debug tree required no additional compilation after the eight-worker focused
target build. Frontend/elaboration, audit, catalog/source, scoped locals, type
and package generics, overloads, generic subprograms, configurations, analysis
order, and components passed 14/14 in 5.43 seconds; the updated case took 0.02
seconds and scoped locals 0.86 seconds. Task 5 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection is due at this task
boundary.

Task 5 is focused-complete. Sixteen stable `CF-VHDL-*` expectation markers now
cross-reference the pinned Apache-2.0 IEEE package source or UVVM across the
existing compact merged runtime applications, without importing upstream test
text. The mapped cases cover expression and aggregate typing, sequential and
concurrent execution, suspending callables and waits, reports/assertions,
direct files and TextIO, access ownership, protected state, physical values,
projected transactions, and the complete reviewed IEEE package integration;
the report and TextIO mappings include exact runtime-failure expectations.
Their retained gates already compare interpreter with LLVM O0/O2 cold/warm or
edit runs through exact values, time, report metadata, file bytes, VCD,
debugger state, specialization/cache identity, and failure policy. The complete
Debug tree rebuilt all seven touched application sources with eight workers.
The audit/catalog/source, scoped-local, report, file/TextIO, procedure/wait,
IEEE integration, record aggregate, advanced type, and projected-transaction
focus passed 12/12 in 4.29 seconds; scoped locals took 0.87 seconds. Task 6 is
current; no sanitizer, Release, full regression, commit, push, or CI inspection
is due at this task boundary.

Task 6 is focused-complete. Nine stable `CF-MIX-*` expectation markers map
fsim's original Apache-2.0 mixed-language contract to `SRC-FSIM`; no external
suite is treated as owning that binding behavior. The retained cases prove
SystemVerilog-to-VHDL and VHDL-to-SystemVerilog construction actuals,
generated hierarchy paths, the full width/signedness/Boolean/integer/two- and
four-state conversion matrix, resolved driver ownership, zero/inertial/
transport/reject/transition/region/postponed boundary timing, canonical source
and DesignIR identity, and exact missing-boundary failure diagnostics. They
retain interpreter/LLVM O0/O2/debug, VCD, callback, cold/warm, and edit
differentials. The complete Debug tree rebuilt the four touched mixed suites
with eight workers. The audit/catalog/source/mixed-matrix, full application,
scoped-local, resolution, conversion, and typed-boundary focus passed 10/10 in
15.65 seconds; scoped locals took 0.81 seconds. Task 7 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection is due at this task
boundary.

Task 7 is focused-complete. Sixteen stable `CF-SC-*` expectation markers
cross-reference fsim's independently authored facade, strict-C ABI, plug-in,
compiler/cache, and application scheduling cases to the pinned Apache-2.0
SystemC source without importing upstream test text. They cover module/process
facade construction, named hierarchy, datatypes, primitive channels, signals,
method/thread/event/update scheduling, lifecycle, append-only ABI layout,
transactional plug-in registration, dependency-aware compiler caching, and
exact unsupported/failure containment. The retained dual SystemVerilog/VHDL
host matrix proves interpreter/LLVM O0/O2, debugger/VCD, callback ordering,
cold/warm cache, and portable plug-in behavior. The complete Debug tree rebuilt
all five touched compilation units with eight workers. Audit/catalog/source,
facade, strict-C ABI, loader, compiler, plug-in matrix, application matrix,
datatypes, and scoped locals passed 12/12 in 52.46 seconds; the application
matrix took 45.49 seconds and scoped locals 0.85 seconds. Task 8 is current; no
sanitizer, Release, full regression, commit, push, or CI inspection is due at
this task boundary.

Task 8 is focused-complete. Nineteen stable `CF-COMMON-*` expectation markers
cross-reference fsim's independently authored common runtime and tool contract
to the pinned cocotb, LLVM, and Tcl behavioral references or to `SRC-FSIM`
where the API is fsim-specific, without importing upstream test text. They
cover scheduler/SimIR ordering and containment, VCD, LLVM O0/O2 validation and
cache rejection, strict C/public API layout, callback and handle lifecycle,
debugger/CLI/trace behavior, logical source provenance, project diagnostics,
and Tcl success/failure semantics. The retained tests exercise exact values,
phases, source points, callback order, stale/reentrant handles, cold/warm cache,
debugger transcripts, trace bytes, and contained failures. The complete Debug
tree rebuilt eight touched compilation units with eight workers. Project,
audit/catalog/source, Tcl selection/application, LLVM, full application,
scoped locals, line directives, C/C++ API, and runtime passed 14/14 in 16.11
seconds; scoped locals took 0.94 seconds. Task 9 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection is due at this task
boundary.

Task 9 is focused-complete. The new exact
[`tests/feature_matrix/v1_conformance_corpus.txt`](../tests/feature_matrix/v1_conformance_corpus.txt)
maps 105 unique adjacent expectation markers in 28 fixtures to 27 registered
CTest owners and their exact evidence modes. The new
`fsim.v1-conformance-corpus` gate derives the ID/source/expectation/file set
from source, pins its sorted SHA-256 as
`49f5754862e3785fa964621b770f202d1d1ad5fb21bd4144b016b7ceb47dd5dd`,
and rejects malformed/duplicate IDs, unsupported provenance, missing fixture
or CTest ownership, invalid expectation-specific modes, missing source-map or
portable-path evidence, count/digest drift, and any absent required frontend,
elaboration, interpreter, LLVM O0/O2, cold/warm/edit cache, debugger, callback,
normalized-VCD, ABI/API, plug-in/compiler, Tcl/CLI, lifecycle, project, or
compile mode. The complete Debug tree reconfigured with eight workers and
required no compilation. Every mapped owner plus the audit/catalog/source,
legality, corpus, and scoped-local gates passed 33/33 in 75.06 seconds; the
SystemC application matrix took 45.39 seconds, the corpus gate 0.07, and scoped
locals 0.82 seconds. Task 10 is current and now owns sanitizer, full Debug and
Release regressions, final documentation/license validation, commit, and push;
no CI inspection is due for Batch 128.

Task 10 is complete. The Apache-2.0 conformance audit, corpus manifest,
architecture, diagnostics, README, and release records now describe one
independently authored and provenance-pinned v1 differential suite. The final
gates catalog all 1,624 production diagnostics, keep all 422 authored sources
within the 2,000-line limit, close all 1,080 required release rows, and protect
105 exact conformance expectations in 28 fixtures owned by 27 CTests with
digest `49f5754862e3785fa964621b770f202d1d1ad5fb21bd4144b016b7ceb47dd5dd`.
The LLVM-disabled ASan/UBSan regression passed all 84 tests in 603.91 seconds
with leak detection disabled only for the managed ptrace restriction. Exact
LLVM 22.1.8 warnings-as-errors Debug passed 87/87 in 262.92 seconds, including
scoped locals in 0.88 seconds; Release passed 87/87 in 217.47 seconds,
including SystemC matrix in 42.31 seconds, containers in 97.84 seconds, the
new conformance application in 4.29 seconds, and scoped locals in 0.82
seconds. All ten Batch 128 tasks are complete. Batch 128 is not a mandatory CI
inspection boundary, so no GitHub Actions run was inspected.

### Batch 129 — Linux and Windows language portability — Complete

The current ten implementation tasks are:

1. **Complete.** Audit the complete v1 implementation and test corpus for
   compiler, standard-library, ABI, endianness, path, newline, process,
   filesystem, timing, stack, and resource assumptions; create an exact
   Linux/Windows Debug/Release portability inventory and repair queue.
2. **Complete.** Close GCC and Clang Debug/Release warning, diagnostic,
   sanitizer, optimization, and language-behavior differences across the
   frontend, elaborator, interpreter, runtime, API, tools, and tests.
3. **Complete.** Close MSVC Debug frontend/elaboration portability, including
   recursive-frame size, exception behavior, source encoding, filesystem/path,
   newline, temporary-file, and deterministic diagnostic differences.
4. **Complete.** Close MSVC Release optimization and runtime portability,
   including lifetime, initialization, aliasing, integer conversion, iterator,
   concurrency, and deterministic scheduling behavior.
5. **Complete.** Close Windows LLVM O0/O2 JIT and debugger portability,
   including object format, symbols, calling convention, stack/unwind,
   dynamic-library, cache, and source-map behavior.
6. **Complete.** Close SystemC facade, strict-C ABI, plug-in/compiler/cache,
   thread, lifecycle, dependency discovery, command-line, and dynamic-library
   portability on Linux and Windows.
7. **Complete.** Close C/C++ API, CLI, Tcl, debugger, VCD, file/memory I/O,
   Unicode/path, environment, callback, failure-containment, and exit-status
   portability on Linux and Windows.
8. **Complete.** Harden build/test infrastructure for bounded four-worker CI,
   eight-worker local builds, executable stack/heap/link limits, test timeouts,
   deterministic fixtures, and actionable phase-level failure evidence.
9. **Complete.** Prove the complete portability corpus with exact Debug/Release,
   interpreter/LLVM O0/O2, cold/warm/edit, API/ABI, plug-in, debugger/VCD, and
   normalized path/newline differentials in the checked-in test matrix.
10. **Complete.** Update matrix/diagnostics/architecture/portability/release
    docs, pass sanitizer, source/catalog and full local Debug/Release gates,
    then create and push the single Batch 129 checkpoint. Hosted Linux/Windows
    release-candidate evidence remains assigned to the mandatory Batch 130 CI
    boundary.

Batch status is **complete**. This exact ten-task list remains the official
Batch 129 record in both the plan and this handoff. Tasks 1 through 9 used one
accumulated dirty worktree with focused eight-worker local builds and tests;
Task 10 owned the sanitizer, full regressions, documentation, commit, and push
gate. GitHub builds use parallelism four. Batch 129 was not a mandatory
non-documentation CI-inspection boundary.

Task 1 is focused-complete. The new
[`docs/v1-portability-audit.md`](v1-portability-audit.md) freezes the supported
64-bit x86-64 Linux/Windows host contract, twelve hosted configuration jobs,
six exact four-worker CI build steps, eight-worker local/link concurrency,
stack and timeout bounds, cross-platform normalization rules, and the sixteen
authored C/C++ source, header, and test files with explicit Windows/compiler
branches. Eight stable `B129-T*` queues route GNU/Clang, MSVC Debug, MSVC
Release, Windows LLVM, SystemC, tools/API, resources, and complete differential
closure to Tasks 2 through 9. The new `fsim.v1-portability-audit` gate protects
the workflow jobs and parallelism, presets, OS/architecture contract, exact
platform-file inventory, invariants, and queue IDs. It performs no network or
hosted-CI access. The Debug tree reconfigured and required no compilation in
an eight-worker build; portability/conformance/catalog/source/scoped-local
gates passed 6/6 in 1.15 seconds, with scoped locals in 0.84 seconds. Task 2 is
current; no sanitizer, Release, full regression, commit, push, or CI inspection
is due at this task boundary.

Task 2 is focused-complete. A complete LLVM/Tcl-disabled Clang 22.1.8 Debug
tree now compiles with eight workers and warnings as errors against the
installed GCC 13 C++ runtime. The compile exposed and repaired explicit
portability gaps in JSON/string byte signedness, bounded container iterator
distances, `countl_zero`/`popcount` result types, ranges count accumulation,
the public API member-size macro's C-style cast, and Tcl-disabled return-code
constants. The API macro also no longer has a redundant implementation-file
definition. The Clang focused frontend/elaboration/runtime/API/application and
ABI gate passed 17/17 in 7.56 seconds. Its standalone SystemC compiler test
passed, while the full plug-in matrix is routed to Task 6 because this local
Clang installation requires an explicit GCC-runtime discovery flag for spawned
compiler invocations. The corresponding GCC+LLVM Debug focus passed 18/18 in
14.42 seconds, including the SystemC matrix in 0.70, conformance in 4.21,
typed boundaries in 3.88, and scoped locals in 0.80 seconds. Full sanitizer
and Debug/Release regressions remain owned by Task 10. Task 3 is current; no
commit, push, or CI inspection is due at this task boundary.

Task 3 is focused-complete. The common lexer now treats one leading UTF-8 BOM
as transport metadata for both SystemVerilog and VHDL while retaining physical
byte offsets; CRLF/CR/LF line accounting is unchanged. New frontend evidence
parses BOM+CRLF SystemVerilog directly and through preprocessing, parses VHDL,
and retains exact Windows-style logical/physical paths and the line-two span
for a negative diagnostic. The matching elaboration fixture retains a four-bit
signal and initial process from the same transport form. The MSVC-compatible
8 MiB stack reserve is centralized in `fsim_configure_test_platform` and now
covers the three C test executables as well as every C++ test host. The new
`fsim.msvc-debug-contract` gate protects that coverage, Windows Debug/Release
workflow presets and four-worker builds, and the scoped/container/application
timeout bounds. The eight-worker GCC+LLVM Debug focus passed 9/9 in 1.55
seconds, with scoped locals in 0.92; the incremental Clang tree compiled
warning-clean and its matching five-test focus passed in 0.18 seconds. Task 4
is current; no sanitizer, Release, full regression, commit, push, or CI
inspection is due at this task boundary.

Task 4 is focused-complete. MSVC-compatible C++ test hosts now select
`/UNDEBUG` explicitly for both MSVC and clang-cl frontends, keeping fixture
assertions live in optimized builds. The new `fsim.msvc-release-contract`
gate protects the native and LLVM Windows Release matrices, CRT and iterator
ABI selections, four-worker hosted builds, and the SystemC plug-in compiler's
exact `/O2` versus `/Od /Z7` policy. Executable command-plan assertions also
retain one matching CRT option per compile/link command, unique objects, and
deterministic PDB/import-library outputs without incremental linking. The
eight-worker GCC+LLVM Debug build was warning-clean and the focused runtime,
API, SystemC, application, portability, and MSVC contract gate passed 11/11 in
13.32 seconds, including scoped locals in 0.84 seconds. Full local Release
equivalence remains owned by Task 10. Task 5 is current; no sanitizer, full
regression, commit, push, or CI inspection is due at this task boundary.

Task 5 is focused-complete. The application cache target now includes the
complete `x86_64-pc-windows-msvc` or `x86_64-unknown-linux-gnu` ABI
environment, preventing native artifacts from sharing an incomplete target
identity. The strict C test now freezes the 24-byte resume-result layout and
its status, instruction, and delay offsets. The new
`fsim.windows-llvm-contract` gate protects x64-only support, LLVM's detected
triple/data layout/CPU/sorted features, O0/O2 cache separation, source/debug
provenance, symbol conversion, Windows atomic replacement, safe DLL search and
ownership, and all four MSVC/clang-cl Debug/Release hosted combinations. The
eight-worker GCC+LLVM Debug build was warning-clean and the focused cache, JIT,
C ABI, application, API, SystemC, and portability gate passed 10/10 in 13.63
seconds, including the complete LLVM unit in 2.75 and scoped locals in 0.84.
Hosted PE/COFF execution remains assigned to Batch 130. Task 6 is current; no
sanitizer, Release, full regression, commit, push, or CI inspection is due at
this task boundary.

Task 6 is focused-complete. The SystemC application matrix now inherits any
required `CMAKE_CXX_COMPILER_ARG1` and global compiler-discovery options through
a generated executable launcher. The launcher and its reported compiler
identity are hashed as the selected compiler, so an installed Clang requiring
`--gcc-install-dir` can reproduce the parent toolchain without turning the
manifest into an unsafe raw-option plan or weakening cold/warm reuse,
corruption recovery, edit invalidation, and concurrent publication checks. The
new `fsim.systemc-portability-contract` gate protects that path along with
GNU/MSVC commands, Windows response/process policy, strict C ABI negotiation,
exception containment, loader ownership, threads, and lifecycle. The complete
Clang SystemC focus passed 4/4 in 3.39 seconds; the eight-worker GCC+LLVM build
was warning-clean and its focused gate passed 9/9 in 52.59 seconds, including
the application matrix in 45.98 and scoped locals in 0.84. Task 7 is current;
no sanitizer, Release, full regression, commit, push, or CI inspection is due
at this task boundary.

Task 7 is focused-complete. New shared `path_from_utf8` and `path_to_utf8`
helpers establish UTF-8 as the public/tool representation while preserving
native `std::filesystem::path` operations. The CLI, C API, project loader,
source/cache identities, diagnostics, Tcl, and runtime file paths now use that
seam. Windows command entry uses `wmain` plus strict UTF-16-to-UTF-8 conversion,
and environment lookup uses the wide Win32 API while retaining unset versus
empty values. The C API test now runs its full project matrix beneath a Unicode
directory; the CLI fixture parses and executes a Unicode direct source,
include, and VCD path. The new `fsim.tool-portability-contract` gate protects
those conversions plus binary file mode and exact 0/1/2/3 CLI statuses. The
eight-worker GCC+LLVM focused API/CLI/Tcl/debugger/VCD/file gate passed 8/8 in
12.58 seconds; the warning-clean Clang build and matching focus passed 6/6 in
4.24 seconds, with scoped locals in 0.02. Task 8 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection is due at this task
boundary.

Task 8 is focused-complete. The new `fsim.resource-portability-contract` gate
counts exactly six `--parallel 4` hosted build steps and protects the default
eight-job Ninja link/archive pool, compact `-Og`/compressed Debug information,
common 8 MiB MSVC-compatible test stack, and exact 60-second scoped-local,
120-second API, 600-second SystemC/aggregate Windows, and 1200-second container
bounds. The separately named SystemC matrix now has its own 600-second bound,
and the aggregate Windows condition covers clang-cl as well as MSVC in every
configuration. Scoped-local O0/O2/warm and SystemC integration/scheduling phase
traces remain machine-checked. The eight-worker build required no compilation;
the new contract and scoped-local test passed 2/2 in 0.85 seconds, while the
same accumulated tree's SystemC phase matrix had already passed in 45.98
seconds. Task 9 is current; no sanitizer, Release, full regression, commit,
push, or CI inspection is due at this task boundary.

Task 9 is focused-complete. The new
[`docs/v1-portability-corpus.txt`](v1-portability-corpus.txt) contains 20 exact
rows naming the portability ID, surface, required modes, owning CTest, evidence
file, and required source marker. `fsim.v1-portability-corpus` rejects malformed
or duplicate rows, missing `PORT-001` through `PORT-020`, absent registrations,
evidence files or markers, and incomplete Debug/Release, interpreter, LLVM
O0/O2, cold/warm/edit, API/ABI, plug-in, callback, debugger/VCD, file,
path/newline, and resource coverage. The eight-worker build required no
compilation. All 20 unique corpus/gate owners passed in 71.55 seconds, including
SystemC integration/scheduling in 45.86, typed boundaries in 4.09, LLVM in
2.60, Unicode API in 1.22, and scoped locals in 0.83. Task 10 is current and
now owns the accumulated sanitizer, source/catalog, full Debug/Release,
documentation, single commit, and push. CI inspection remains deferred to the
mandatory Batch 130 boundary.

Task 10 is complete. The LLVM-disabled ASan/UBSan regression passed all 92
tests in 545.85 seconds with leak detection disabled only for the managed
ptrace restriction; the SystemC application matrix passed in 141.70 seconds,
the container application in 274.03 seconds, and scoped locals in 0.61
seconds. Exact LLVM 22.1.8 warnings-as-errors Debug passed 95/95 in 249.39
seconds, including SystemC matrix in 45.31, containers in 125.84, and scoped
locals in 0.89 seconds. Release passed 95/95 in 210.55 seconds, including
SystemC matrix in 41.59, containers in 95.41, and scoped locals in 0.82
seconds. Final gates cover 1,624 production diagnostics, 424 authored sources
within the 2,000-line limit, 1,080 required release rows with no explicit
evidence gaps, 105 conformance expectations in 28 fixtures owned by 27 CTests,
and 20 exact portability rows. All ten Batch 129 tasks are complete; no GitHub
Actions run was inspected at this non-mandatory boundary.

### Batch 130 — Final v1 release audit — Complete

The current ten implementation tasks are:

1. **Complete.** Audit every v1 feature-matrix row against checked-in syntax,
   legality, elaboration, interpreter, LLVM, cache/provenance, debugger/trace,
   and portability evidence; create the exact final closure queue.
2. **Complete.** Re-audit the complete SystemVerilog v1 subset and repair every
   remaining positive, negative, diagnostic, elaboration, or execution gap.
3. **Complete.** Re-audit the complete VHDL v1 subset and repair every remaining
   positive, negative, diagnostic, elaboration, or execution gap.
4. **Complete.** Re-audit mixed-language and SystemC boundaries, conversions,
   scheduling, lifecycle, plug-in, compiler, cache, and strict-C ABI evidence.
5. **Complete.** Close final interpreter versus LLVM O0/O2, cold/warm/edit,
   debugger, VCD, scheduling, and failure differential gaps across the corpus.
6. **Complete.** Validate installed/public CLI, API, ABI, Tcl, runtime, package,
   path, environment, callback, and exit-status release behavior.
7. **Complete.** Reconcile diagnostics, source-size, license, conformance, and
   provenance inventories and eliminate every unowned or stale release gap.
8. **Complete.** Validate final Linux/Windows Debug/Release build, memory, stack,
   timeout, concurrency, and phase-trace contracts for bounded hosted runs.
9. **Complete.** Freeze the final release-candidate corpus and reclassify every
   completed v1 matrix row with exact owning tests and evidence links.
10. **Complete.** Update all v1/release records, pass sanitizer,
    source/catalog and full local Debug/Release gates, create and push the
    final checkpoint, then inspect and repair every non-documentation job at
    the mandatory Batch 130 GitHub CI boundary.

Batch status is **complete**. All local Task 10 gates and all 12 mandatory
non-documentation GitHub configurations pass. GitHub builds use parallelism
four.

Task 1 is focused-complete. The new
[`docs/v1-release-audit.md`](v1-release-audit.md) freezes the 1,080-row matrix
baseline and its digest across 671 `SV`, 9 `V1-SV`, 252 `VH`, 8 `V1-VH`, 16
`ML`, 28 `SC`, 86 `CM`, and 10 `V1-CM` rows. Eight exact `B130-T*` queues
route the complete SystemVerilog, VHDL, mixed/SystemC, differential, public,
inventory, resource, and final reclassification reviews to Tasks 2 through 9.
The new `fsim.v1-release-audit` gate composes the diagnostic, source-size,
legality, conformance, portability, and IEEE-package inventories, independently
checks every row ID/count and queue, and rejects unreviewed matrix changes. The
eight-worker Debug build required no compilation; the focused audit and scoped
local gate passed 9/9 in 1.95 seconds, with the composed release audit in 0.58
and scoped locals in 0.83 seconds. Task 2 is current; no sanitizer, Release,
full regression, commit, push, or CI inspection is due at this task boundary.

Task 2 is focused-complete. The new
[`docs/v1-systemverilog-release-audit.md`](v1-systemverilog-release-audit.md)
partitions syntax, semantics, execution, native differential, and release
ownership for all 671 `SV` and 9 `V1-SV` rows. Its strict gate requires
executable status, complete typed evidence ownership, a test-owned runtime cell,
and no stale missing-test or implementation-only runtime claim; the current
matrix names 48 distinct runtime evidence files. The audit exposed and repaired
stale SV-011/SV-012 expression links, added three-mode built-in gate
`min:typ:max` execution, added typical-branch nonblocking named-event triple
execution, and added classic-callable missing/extra/duplicate/delimiter/end-name
diagnostics. The eight-worker Debug build was warning-clean after adding the
required algorithm header. Frontend, named-event, transition-delay, legality,
release/SV audits, and scoped locals passed 7/7 in 3.84 seconds, including
transition delays in 0.71, named events in 0.86, and scoped locals in 0.80
seconds. Task 3 is current; no sanitizer, Release, full regression, commit,
push, or CI inspection is due at this task boundary.

Task 3 is focused-complete. The new
[`docs/v1-vhdl-release-audit.md`](v1-vhdl-release-audit.md) partitions syntax,
semantics, packages/provenance, execution, and release ownership for all 252
`VH` and 8 `V1-VH` rows. Its strict gate composes the final release audit and
requires executable status, complete evidence ownership, and a test-owned
runtime cell; all 260 rows pass with 40 distinct runtime evidence files and no
stale or unowned claim. The eight-worker Debug build required no compilation.
The complete VHDL-labeled slice passed 30/30 in 16.54 seconds, including typed
boundaries in 3.80, arrays in 2.03, overloads in 1.85, logic9 in 1.15, and
components in 0.99 seconds; the separate scoped-local guard passed in 0.80
seconds. Task 4 is current; no sanitizer, Release, full regression, commit,
push, or CI inspection is due at this task boundary.

Task 4 is focused-complete. The new
[`docs/v1-mixed-systemc-release-audit.md`](v1-mixed-systemc-release-audit.md)
partitions mixed type conversion, boundary scheduling, SystemC facade/ABI,
lifecycle/compiler/cache, and native differential ownership for all 16 `ML`
and 28 `SC` rows. Its strict gate composes the final release audit and requires
executable status, complete evidence ownership, and a test-owned runtime cell;
all 44 rows pass with 14 distinct runtime evidence files and no stale or
unowned claim. The eight-worker Debug build required no compilation. The
complete mixed/SystemC-labeled slice passed 9/9 in 55.20 seconds, including the
SystemC application matrix in 46.19, typed boundaries in 3.84, datatypes in
2.54, mixed conversions in 0.77, and resolution in 0.49 seconds; scoped locals
passed separately in 0.83 seconds. Task 5 is current; no sanitizer, Release,
full regression, commit, push, or CI inspection is due at this task boundary.

Task 5 is focused-complete. The new
[`docs/v1-differential-release-audit.md`](v1-differential-release-audit.md)
composes the final language, conformance, and portability audits and freezes
94 distinct runtime evidence files plus 36 corpus CTests. Across the 1,080
matrix rows it records 439 interpreter, 361 LLVM/compiled/native, 244
cache/reuse/edit, 90 debugger, 116 VCD/trace, 394 scheduling/time/event, and
89 failure/diagnostic/callback/ABI runtime claims; categories overlap by
design. The gate also requires interpreter, LLVM O0/O2, cold/warm/edit,
debugger, VCD, callback, diagnostic, lifecycle, ABI, plug-in, compiler,
source-map, and portable-path modes in the corpus union. The eight-worker Debug
build required no compilation. The 19 unique portability owners plus the new
gate passed 20/20 in 72.37 seconds, including SystemC matrix in 45.20, the
application in 9.24, typed boundaries in 3.88, LLVM in 2.68, and scoped locals
in 0.81 seconds. Task 6 is current; no sanitizer, Release, full regression,
commit, push, or CI inspection is due at this task boundary.

Task 6 is focused-complete. The new
[`docs/v1-public-release-audit.md`](v1-public-release-audit.md) freezes the
five installed commands, five public header groups, two libraries, API and
SystemC ABI version 1, CLI statuses 0/1/2/3, and UTF-8/native path and Windows
environment seams. `fsim.installed-public-contract` creates a fresh Unicode
prefix without network access, verifies the exact installed command, library,
header, documentation, and IEEE-package layout and byte-identical public
headers, then executes installed version, alias help, and invalid-option
behavior. The eight-worker Debug build required no compilation. The public
audit, staged install, CLI, C/C++ API/ABI, Tcl relocation, runtime-file, and
scoped-local focus passed 11/11 in 14.05 seconds, including the staged install
in 0.61 and scoped locals in 0.87 seconds. Task 7 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection is due at this task
boundary.

Task 7 is focused-complete. The new
[`docs/v1-inventory-release-audit.md`](v1-inventory-release-audit.md) and
`fsim.v1-inventory-release` gate compose and freeze 1,624 production
diagnostics, 424 authored sources under the 2,000-line cap, 500 SPDX-owned
repository artifacts, the 31-file/26-VHDL checksummed IEEE snapshot, 105
conformance expectations in 28 fixtures owned by 27 CTests, and 10 reviewed
plus 6 excluded provenance identities. The audit rejects an unlicensed owned
artifact, stale diagnostic, source-size exception, unreviewed third-party
root, changed package byte, or changed expectation/source/owner digest. The
eight-worker Debug build required no compilation. Inventory owners,
SystemVerilog conformance runtime, and scoped locals passed 8/8 in 5.70
seconds, including the composed inventory in 0.35, conformance runtime in
4.20, and scoped locals in 0.85 seconds. Task 8 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection is due at this task
boundary.

Task 8 is focused-complete. The new
[`docs/v1-resource-release-audit.md`](v1-resource-release-audit.md) and
`fsim.v1-resource-release` gate compose the complete platform contracts and
freeze 12 hosted configurations, six four-worker CI build steps, the local
eight-link pool, compact Debug objects, 8 MiB MSVC-compatible test stacks,
20/45/70-minute job bounds, 60/120/600/900/1200-second test classes, scoped/SystemC
phase traces, 14 explicit platform files, and 20 portability rows. This was a
static local inspection only; no GitHub Actions state was queried. The
eight-worker Debug build required no compilation. Resource/portability owners,
the SystemC matrix, and scoped locals passed 11/11 in 46.30 seconds, including
SystemC in 45.30, scoped locals in 0.84, and the composed audit in 0.07 seconds.
Task 9 is current; no sanitizer, Release, full regression, commit, push, or CI
inspection is due at this task boundary.

Task 9 is focused-complete. The exact
[`docs/v1-release-candidate-corpus.txt`](v1-release-candidate-corpus.txt) and
`fsim.v1-release-candidate` gate reclassify all 1,080 required matrix rows as
`execute`, require links in all 4,320 P+/P-/E/R cells, resolve every link to a
checked-in owner, and freeze 320 evidence paths: 157 test, 148 production, and
15 documentation/build/release paths. The corpus also retains 94 distinct
runtime evidence files and 36 conformance/portability CTests. Both the exact
matrix bytes and evidence-path set are digest-pinned. The eight-worker Debug
build required no compilation. The nine final release audits and scoped locals
passed 10/10 in 12.75 seconds, including the candidate gate in 5.75 and scoped
locals in 0.85 seconds. Task 10 is current and owns sanitizer, source/catalog,
full Debug/Release, documentation, the single commit, and push. Stop before
mandatory CI monitoring afterward.

Task 10 local release work is complete. The LLVM-disabled ASan/UBSan tree was
built with eight workers and passed 102/102 tests in 553.65 seconds with leak
detection disabled only for the managed ptrace restriction; SystemC took
140.41 seconds, containers 269.82, transition delays 4.00, and scoped locals
0.62. Exact LLVM 22.1.8 warnings-as-errors Debug passed 105/105 in 259.95
seconds, including SystemC in 45.16, containers in 124.68, transition delays
in 0.73, and scoped locals in 0.84. Release passed 105/105 in 223.76 seconds,
including SystemC in 42.05, containers in 95.18, transition delays in 0.64,
and scoped locals in 0.83. The final gates freeze 1,624 diagnostics, 424
bounded C/C++ sources, 500 SPDX-owned artifacts, 1,080 execute rows, 4,320
linked evidence cells, 105 conformance expectations, 20 portability rows, and
the staged installed public contract. `git diff --check` and the new-document
relative-link check pass. The single checkpoint commit and push follow this
record. Stop before querying GitHub Actions; hosted inspection remains the
only incomplete Batch 130 action.

The first mandatory hosted run, GitHub Actions `30820714702` for checkpoint
`286ba18`, accepted all 12 configurations and passed the frontend fuzz lane.
Windows MSVC Debug then failed during compilation before tests with C2397 at
`src/app/application_analysis.cpp`: list initialization implicitly narrowed
the SystemC C-ABI `fsim_sc_edge_kind_v1` to the validation model's
`std::uint32_t`. The repair makes that width conversion explicit at the
untrusted ABI metadata seam and extends `fsim.msvc-debug-contract` to reject a
regression. Exact-LLVM Debug and Release rebuilt warning-clean with eight
workers; the core application, SystemC compiler/matrix/application, mixed and
typed boundaries, Windows contracts, and scoped locals passed 10/10 in 63.34
seconds. Repair checkpoint `9748d87` was pushed and replacement run
`30821583086` crossed the original failure point, then exposed MSVC warning
C4702 as an error in the standalone SystemC header test: the statement after
the true arm of `custom_interface_kind` was unreachable for interfaces with a
custom kind. The follow-up expresses the fallback as the `else` arm of the
`if constexpr` and extends the MSVC contract to pin that warning-clean shape.
Exact-LLVM Debug and Release rebuilt with eight workers, and the standalone
SystemC header plus MSVC contract passed 2/2 in each tree. The second repair
checkpoint `35faf4a` was pushed. Replacement run `30822527888` passed fuzz and
compiled the repaired header, then exposed C4996 in the SystemC plug-in
compiler test because its environment save/restore helper called deprecated
`std::getenv` directly. The follow-up routes all three test environment reads
through the production UTF-8/native environment adapter and makes the MSVC
contract reject direct `getenv` reintroduction. Exact-LLVM Debug and Release
rebuilt with eight workers; the plug-in compiler and MSVC contract passed 2/2
in 2.81 and 2.39 seconds. The third repair checkpoint and replacement hosted
run are pending. Checkpoint `501a031` was pushed; replacement run
`30823335803` passed fuzz and both repaired compile points, then plain MSVC
Debug and MSVC/exact-LLVM Debug exposed the same C2026 failure in the merged
application fixture: one generated SystemC translation unit occupied a
24,844-byte raw string. The follow-up partitions it into six independent
stream chunks no larger than 12,095 bytes and extends the MSVC contract to pin
that shape. Exact-LLVM Debug and Release rebuilt with eight workers, and the
SystemC application matrix plus contract passed 2/2 in 45.09 and 42.04
seconds. The fourth repair checkpoint and replacement hosted run are pending.

The fourth repair checkpoint `8876a8c` was pushed, and replacement run
`30824506517` compiled every job successfully. All six Ubuntu jobs passed:
GCC Debug/Release, exact-LLVM Debug/Release, ASan/UBSan, and the Clang fuzz
lane. All six Windows jobs then exposed one shared test-portability
cluster: checkout-native CRLF changed the pinned feature-matrix byte digest;
UTF-8 generic source names were compared with native path spellings; the
Windows loaded-module probe fell back to a colliding plug-in filename; two
fixtures retained obsolete no-cache expectations after complete MSVC
dependency discovery became cacheable; and the Unicode C API fixture reached
an incomplete UTF-8-to-native conversion in preprocessing. Plain MSVC Release
also exposed one intermittent concurrent `cl.exe` dependency-scan/cache-
publication failure. The follow-up normalizes matrix line endings before
hashing, completes native/UTF-8 conversions at project, preprocessor,
analysis, cache, debugger, and Tcl seams, compares existing test paths by
filesystem identity, requires exact-path Windows module lookup, shares the
cache-hit expectations across hosts, and serializes the complete Windows
plug-in compile transaction in-process while retaining the per-key
cross-process lock. The source and license inventories advance to 424 and
500, and the explicit-platform inventory contracts to 14 because the two
application fixtures no longer need native-only branches. Exact-LLVM Debug
and Release rebuilt warning-clean with eight workers and passed complete
105/105 regressions in 261.01 and 227.84 seconds; scoped locals remained quick
at 0.85/0.84 seconds. The final clang-cl Release job confirmed the concurrency
defect by timing out `fsim.systemc.compiler` at its 1,500-second bound, while
plain MSVC Release failed an assertion inside that same three-caller section;
the Windows transaction serialization directly owns both outcomes. Fifth
repair checkpoint `38a0bcd` was pushed; replacement run `30828460860` is the
current mandatory hosted proof.

Replacement run `30828460860` passed all six Ubuntu jobs, including exact
LLVM Debug/Release, ASan/UBSan, and fuzz. All six Windows jobs then reached the
complete test suite and failed the same remaining native-spelling assertion in
`application_test_specialization.cpp`: specialization dependencies are stored
as UTF-8 generic paths, but this older fixture still compared them with
`std::filesystem::path::string()`. Every Windows `fsim.systemc.compiler` test
passed quickly, from 8.43 to 24.29 seconds, so the prior Release concurrency
failure and hang are repaired; scoped locals also passed in 0.08 to 1.22
seconds. Plain MSVC Debug additionally reached the 600-second SystemC
application-matrix bound, while the five sibling Windows configurations passed
that matrix in 460.06 to 558.97 seconds. The sixth consolidated repair routes
all remaining specialization dependency checks through the shared UTF-8/native
filesystem-identity helper, statically rejects the original comparison, and
sets a still-bounded 900-second SystemC matrix limit. Exact-LLVM Debug and
Release rebuilt warning-clean with eight workers; the application and three
resource/MSVC contracts passed 4/4 in 9.92 and 9.56 seconds. Sixth repair
checkpoint `379916e` was pushed; replacement run `30831982084` is the current
mandatory hosted proof.

Replacement run `30831982084` passed all six Ubuntu jobs: GCC Release in
12m23s, fuzz in 2m16s, exact-LLVM Debug/Release in 19m44s/19m37s, ASan/UBSan
in 38m25s, and GCC Debug in 13m22s. All six Windows jobs reached
`application_test_cli.cpp:228` and failed the same shared-compilation-unit
assertion that the owning semantic source table contains exactly two files;
no further timeout appeared. The manifest roots remained two, but
preprocessing canonicalized their physical paths while semantic registration
retained the manifest spelling. Windows temporary-directory aliases therefore
caused a parsed span to intern a duplicate semantic source. The seventh
consolidated repair uses one error-safe, generic-UTF-8 canonical identity for
both semantic source registration and parsed spans, and extends the MSVC
contract to pin that canonicalization and the two-source proof. Exact-LLVM
Debug and Release rebuilt warning-clean with eight workers; the application,
release-candidate, resource-release, MSVC, and resource-portability gates
passed 5/5 in 15.73 and 15.23 seconds. Seventh repair checkpoint `b0c7391`
was pushed; replacement run `30835947147` is the current mandatory hosted
proof. The final source, inventory, resource,
release-candidate, MSVC, and portability gates passed 6/6 in 6.25 seconds.

Replacement run `30835947147` passed all six Ubuntu jobs: exact-LLVM Debug
in 19m36s, GCC Release in 10m17s, ASan/UBSan in 39m34s, GCC Debug in 12m39s,
fuzz in 2m24s, and exact-LLVM Release in 17m54s. Five Windows jobs passed the
repaired shared-compilation-unit assertion and every other test, then failed
one downstream typed-boundary fixture assertion: semantic source ownership
now correctly stores the canonical `runneradmin` path, while the test searched
for the equivalent `RUNNER~1` temporary-directory alias by string equality.
Their elapsed times were 38m40s for MSVC/LLVM Release, 38m16s for clang-cl/LLVM
Debug, 31m09s for MSVC Release, 31m48s for MSVC Debug, and 25m20s for
clang-cl/LLVM Release. MSVC/LLVM Debug passed the 900-second SystemC matrix in
715.87 seconds and scoped locals in 1.72 seconds, then the 45-minute job ceiling
cancelled it during the independently bounded 1,200-second container test.
The eighth consolidated repair compares typed-boundary provenance by shared
filesystem identity and raises only the Windows LLVM hosted job ceiling to 70
minutes; per-test bounds and four-worker hosted builds remain unchanged.
Exact-LLVM Debug and Release rebuilt warning-clean with eight workers; typed
boundaries plus the three Windows/resource contracts passed 4/4 in 3.99 and
3.54 seconds. The seven final source, inventory, release, and portability
gates passed in 6.34 seconds. Eighth repair checkpoint `4565ad8` was pushed;
replacement run `30839583995` is the current mandatory hosted proof.

Replacement run `30839583995` passed all 12 mandatory configurations.
Ubuntu timings were fuzz 1m47s, GCC Debug 12m49s, ASan/UBSan 39m07s,
exact-LLVM Release 13m02s, GCC Release 13m43s, and exact-LLVM Debug 20m46s.
Windows timings were MSVC Debug 30m14s, clang-cl/LLVM Debug 37m36s, MSVC
Release 30m58s, MSVC/LLVM Debug 37m25s, MSVC/LLVM Release 37m41s, and
clang-cl/LLVM Release 26m39s. Across the six Windows jobs, the SystemC
application matrix passed in 457.74 to 635.14 seconds, scoped locals remained
quick at 0.18 to 1.41 seconds, and the container differential passed in 44.78
to 702.26 seconds. Batch 130 Task 10 and all ten Batch 130 tasks are complete;
this successful run is the mandatory hosted v1 release proof.
The final documentation/source, inventory, release-candidate, MSVC, Windows
LLVM, and resource-portability closure gates passed 7/7 in 6.31 seconds.

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

## Corrective Batch 131 — container and array resource policy — Complete

This post-v1 corrective batch removes the accidental 4,096-element semantic
cap while retaining explicit, representation-derived host resource safety.
The ten-task batch record is:

1. **Complete:** audit every production and documentation occurrence of the
   4,096 container/array policy and separate unrelated string, parser-scan,
   process-I/O-buffer, and TextIO limits.
2. **Complete:** replace the element constant with a 256 MiB per-owning-
   container storage budget derived from `sizeof(PackedLogic4)`.
3. **Complete:** widen declared queue capacity metadata to 64 bits and allow
   every nonnegative locally constant bounded-queue maximum.
4. **Complete:** apply overflow-safe storage-budget checks to one- and
   multidimensional static arrays, ports, and mixed hierarchy adaptation.
5. **Complete:** apply the common budget to dynamic resize, associative
   insertion, unbounded queues, patterns, and native callbacks while
   preserving source-declared bounded-queue overflow semantics.
6. **Complete:** replace the internal VHDL access heap's 4,096-object default
   with the non-null handle domain capped by the common owning-storage budget.
7. **Complete:** advance native schema 75 and container semantic revision 29
   so the storage policy participates in cache identity.
8. **Complete:** add interpreter and elaboration evidence for 4,097-element
   static arrays and 4,097-entry associative arrays plus oversized-range
   rejection before allocation.
9. **Complete:** update language support, diagnostics, architecture, and the
   feature matrix to distinguish resource budgets from language bounds.
10. **Complete:** exact-LLVM Debug and Release passed 106/106 in 136.89 and
    105.34 seconds. The isolated compiled capacity case passed in 14.05/7.46
    seconds and the unchanged full container differential in 136.88/105.33
    seconds. LLVM-disabled ASan/UBSan passed the four focused cases in 274.94
    seconds; leak detection alone was disabled because LeakSanitizer cannot
    run under the execution environment's tracing layer. Source, diagnostics,
    resource, SystemVerilog-release, release-audit, and release-candidate
    gates pass. The accumulated commit and push follow this record.

## Post-v1 three-language hierarchy example — Complete

- `examples/three_language_hierarchy` is a self-contained project with a
  SystemVerilog testbench top, an explicitly bound SystemC factory and method,
  and an explicitly bound VHDL child.
- The executable path drives SV `stimulus`, inverts it into a SystemC internal
  signal, inverts it again in VHDL, and checks the returned SV value across
  three transitions before printing `PASS` at tick 3.
- The tutorial covers eight-worker local build, check/build/run, explicit
  recursive bindings, cold/warm cache behavior, VCD paths, hierarchy traversal,
  signal breakpoints, live trace selection, mutation commands, and extension
  experiments.
- `debug-session.sh` replays the documented hierarchy walk and stops on
  `three_language_tb.observed == 1` at time 1, delta 2.
- `fsim.application` builds the checked-in manifest through interpreter and
  compiled engines, compares callbacks/final values/normalized VCD, and asserts
  the SystemC and nested VHDL hierarchy objects. The focused test passes in
  10.98/10.42 seconds in exact-LLVM Debug/Release.
- The license inventory intentionally advances from 500 to 506 SPDX-owned
  artifacts. Project, source-line, release-audit, inventory, and
  release-candidate gates pass; generated example cache/VCD outputs are not
  source artifacts.

## Corrective Batch 132 — SystemC HDL proxy and exports — Complete

This additive post-v1 refactor makes HDL-backed children construct and bind
like ordinary SystemC modules while retaining explicit manifest target
selection and legacy plug-in compatibility. The ten-task batch record is:

1. **Complete:** add `hdl_module` and `SC_FSIM_HDL_MODULE` with ordinary
   `sc_in`, `sc_out`, and `sc_inout` binding syntax.
2. **Complete:** append host-ABI callbacks for marking an existing native module
   as HDL-backed and recording immutable scalar construction actuals.
3. **Complete:** lower marked modules to same-path foreign HDL implementations
   using their recorded port declarations and bindings.
4. **Complete:** expose module-like hierarchy identity without a synthetic
   `hdl_instance` child and reject executable SystemC contents in proxies.
5. **Complete:** add a deterministic plug-in-local multi-export registry and one
   support-library initialization entry point.
6. **Complete:** add type-name/alias export macros, parameter-schema discovery,
   duplicate rejection, and legacy compatibility.
7. **Complete:** migrate the three-language example and representative typed
   plug-in fixtures while retaining dedicated legacy coverage.
8. **Complete:** add positive, negative, ABI, loader, cache, debugger, VCD, and
   interpreter/LLVM application evidence.
9. **Complete:** advance the SystemC runtime cache schema and update public,
   architecture, feature-matrix, diagnostic, inventory, and restart records.
10. **Complete:** the eight-worker exact-LLVM Debug and Release builds and full
    regressions pass 106/106 tests in 133.42/102.58 seconds. The eight-worker
    LLVM-disabled ASan/UBSan build and full regression pass 103/103 tests in
    277.35 seconds with LeakSanitizer detection disabled because the execution
    runner uses `ptrace`; AddressSanitizer and UndefinedBehaviorSanitizer remain
    enabled. Source, inventory, public-release, release-candidate, SystemC,
    MSVC, and Windows portability contracts pass. Batch 132 is not a scheduled
    non-documentation CI-inspection boundary.

## Working cadence

- Implement ten related features before the next full regression.
- Use focused warnings-as-errors builds and targeted tests after each coherent
  change; do not run the full suite for every individual feature.
- Keep Tasks 1 through 9 in one accumulated working tree. Update their status
  records without routine per-task sanitizer runs, commits, or pushes; reserve
  those operations for Task 10 unless an explicit restart checkpoint or risky
  structural boundary requires an earlier durable commit.
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
