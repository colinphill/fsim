<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v1 resume handoff

This is the short entry point for resuming v1 implementation. The
[implementation plan](implementation-plan.md) remains the chronological record,
and the [feature matrix](feature-matrix.md) remains the release authority.

## Snapshot

- Recorded: 2026-07-31.
- Branch: `codex/resumable-jit`.
- Implementation baseline: completed feature-batch-90 bounded static-array
  slice ordering mutation on top of the feature-batch-89 callable-slice
  handoff.
- The source-size refactor is complete: all 290 authored C/C++ source, header,
  and test files are at or below the 2,000-line hard limit; the allowlist is
  empty and the maximum is 2,000 lines.
- The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59
  configured tests in 329.46 seconds, and Release passed all 59 configured
  tests in 98.66 seconds on 2026-07-31. Debug/Release scoped locals completed
  in 0.90/0.80 seconds and the expanded container differential completed in
  260.58/67.70 seconds.
- The diagnostic catalog covers all 1,222 production codes.
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
  to two workers to avoid hosted Ubuntu memory pressure. Replacement run
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
  stack. CI jobs retain a 45-minute budget because two-worker ASan and Windows
  builds legitimately exceed 25 minutes.
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

The repository is a substantial pre-alpha executable simulator, not fsim v1.
Many language families have strong bounded evidence, but every broad v1
contract row is release-blocking until it has complete positive, negative,
elaboration, interpreter, and JIT evidence.

## Progress by milestone

| Milestone | Status | Current result |
|---|---|---|
| 1. Platform and semantic spine | In progress | Cross-platform C++20/CMake foundation, exact LLVM adapter, dependencies, diagnostics, manifest, native ABIs, Tcl, and CI definitions exist. Unicode/path and remaining console-interrupt validation are open. |
| 2. Internal vertical slice | In progress, near architecture gate | Interpreter, hybrid LLVM JIT, cache, deterministic scheduler, mixed hierarchy, VCD, debugger, and examples execute. Bounded typed 1–64-bit SystemVerilog constants, same-language type parameters, strings, text files, dynamic arrays/queues/integral-key associative arrays, one-dimensional static memories with `$readmem*`, direct same-language static and dynamic whole-container module ports, direct compatible static-array slice assignment, read-only query/reduction/locator consumers, function/task slice actuals, and atomic slice ordering mutation, unpacked-container bound/size/bit/dimension queries, positional container patterns plus static default/index-key patterns, reductions with bounded pure implicit/named-iterator `with` transformations, deterministic ordering with optional bounded pure `with` keys, extrema/uniqueness locators with optional bounded pure `with` transformations, and predicate locators with named iterators and signed declared/current indices, automatic integral/string/container functions and suspending tasks, full unsigned-64 values, and entity-level VHDL-2008 unclassified interface type, bounded interface function/procedure/package generics, generic subprogram templates/instances, recursive block/generate configurations and references, and scoped/package-visible overloaded scalar/vector and composite component declarations with value/type/function/procedure/package generics, deterministic default binding, typed component input defaults, and disconnected open output-family ports now have interpreter/O0/O2/cache evidence; wider/complete typing, remaining slice boundaries, multidimensional and cross-language container boundaries, remaining VHDL hierarchy/generic semantics, source metadata, and complete differential coverage still block the gate. |
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
- bounded one-dimensional integral SystemVerilog dynamic arrays, queues,
  integral-key associative arrays, and locally constant static unpacked arrays
  with typed module objects and automatic values, copy isolation, indexed
  access, dynamic/queue/associative methods, dense direction-aware fixed
  storage, manifest-confined `$readmemb`/`$readmemh`, suspending-task copy-out,
  direct same-language whole-container module-port aliases across
  nested/generated hierarchy, direct bound/size/bit/dimension system queries
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
- Add interfaces/modports and complete packages.
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
current schema-40 interpreter/native/cache identity.
Unicode
code-point semantics, multidimensional or
aggregate/string-element containers, sliced/expression port actuals,
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

## Next ten-feature batch

Resume with **feature batch 91: bounded static-array slice module-port
actuals**:

1. Retain direct named and positional static-array slice port actuals as
   explicit source-spanned colon `Slice` HIR.
2. Bind a compatible slice to a child input static-array port as a read-only
   ordinal view without exposing unselected parent elements.
3. Bind a compatible writable slice to a child output static-array port and
   atomically update only the selected parent range.
4. Bind a compatible writable slice to a child inout static-array port with
   exact initial visibility and bidirectional selected updates.
5. Map equal-count compatible parent and formal ranges ordinally across
   different declared indices and ascending/descending directions.
6. Preserve exact element width, signedness, two-/four-state domain, and X/Z
   bits without conversion or packed reinterpretation.
7. Keep overlapping or multiply driven selected connections deterministic
   through the existing object/driver ownership and resolution rules.
8. Carry sliced port actuals through nested/generated same-language hierarchy,
   specialization, debugger paths, VCD, interpreter, LLVM O0, and LLVM O2.
9. Diagnose read-only output/inout, count/profile mismatch,
   nonconstant/indexed/indirect/nonstatic/multidimensional, overlapping
   unsupported, recursive, and cross-language forms through stable coverage.
10. Version formal direction/range/profile, selected actual range, binding or
    copy operations, specialization, and transitive source provenance in
    schema 43; add full positive/negative/cache evidence and push the
    non-boundary batch.

Keep Batch 91 to direct locally constant direction-preserving slices of
one-dimensional integral static-array objects connected to already supported
same-language static-array module ports. Expression adapters beyond direct
slices, slice returns, variable/indexed slices, element conversion, unsupported
overlapping drivers, recursion, multidimensional/nonstatic containers, and
cross-language slice boundaries remain separate release-gate work.

## Working cadence

- Implement ten related features before the next full regression.
- Use focused warnings-as-errors builds and targeted tests after each coherent
  change; do not run the full suite for every individual feature.
- Use at least eight parallel workers for every local project, test-support,
  and fetched-dependency build, including interim builds. Prefer
  `cmake --build <tree> --parallel 8` (or a larger value). GitHub Actions is
  the explicit exception: its hosted-VM builds use `--parallel 2` to avoid
  memory pressure.
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
static-array-slice-ordering-mutation handoff. Treat the newest pushed commit
on the same branch as the authoritative continuation and read this file from
that checkout before doing work.

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
resume Batch 91 below and return to focused tests until its tenth feature.

## Resume commands

Start by confirming that no newer implementation supersedes this handoff:

```sh
git status --short --branch
git log -5 --oneline --decorate
gh run view 30613827882 --json status,conclusion,url,headSha,jobs
```

For a clean-context restart:

1. Use `docs/v1-resume.md` as the entry point, `docs/feature-matrix.md` as the
   release authority, and `docs/implementation-plan.md` only when historical
   detail is needed.
2. Confirm the branch is `codex/resumable-jit` and history contains the
   bounded SystemVerilog string, text-file, and container implementations.
3. Begin Batch 91 at item 1 above. Do not rerun Batch 90's full regression
   unless a change can affect slice assignment, consumers, callable or module
   boundary copies/aliases, ordering, container objects/ports, runtime
   helpers, debugger/VCD paths, cache identity, or execution.
4. Keep Batch 91 within direct locally constant direction-preserving slices of
   one-dimensional integral static-array objects connected to the existing
   same-language static-array module-port forms.
   Record intentional scope changes in this handoff before implementation.
5. Use targeted tests during that batch, run the full Debug and Release gates
   after all ten features, then update the four documents named above, commit,
   and push.

The existing exact-LLVM build trees on the recorded development host are:

```sh
cmake --build build/llvm22-ninja-debug --parallel 8
cmake --build build/llvm22-ninja-release --parallel 8
```

Use a narrow test expression while Batch 91 is in progress, extending the
hierarchy, port, and container tests as static-array slice bindings appear:

```sh
ctest --test-dir build/llvm22-ninja-debug --output-on-failure \
  -R 'fsim\.(frontend|runtime|elaboration|llvm|application\.sv_containers|diagnostics-catalog|source-line-budget)'
```

Both warnings-as-errors Ninja trees link the exact LLVM 22.1.8 backend. Their
recorded 59-test inventories are clean after feature batch 90.

Before declaring any row complete, consult:

- [implementation plan and progress](implementation-plan.md);
- [feature matrix and release evidence](feature-matrix.md);
- [language support](language-support.md);
- [cross-language semantic contract](cross-language-semantics.md); and
- [SystemC subset and plug-in model](systemc-subset.md).
