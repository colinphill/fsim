<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v2 restart handoff

Read [implementation_plan_v2.md](implementation_plan_v2.md) first; it is the
authoritative v2 batch/status record. Preserve the completed v1 history in
`v1-resume.md`.

## Batch 150 start checkpoint - 2026-08-05

1. Start in `/home/colin/projects/fsim` and read this file plus the expanded
   Batch 150 and locked Batches 151-175 sections of
   `implementation_plan_v2.md`.
2. Run `git status --short --branch`, `git rev-parse HEAD`,
   `git rev-parse origin/codex/v2`, and `git log -1 --oneline`. The branch and
   origin revisions must match pushed Batch 149 closeout `6279f0b` unless the
   user has intentionally advanced the branch. Batch 149's central
   edits are the semantic class/constraint HIR, application projection and
   call sites, class resolution, deterministic runtime random streams, the
   resource-governed finite-domain solver, exact typed constraint-expression
   lowering, membership/distribution/soft and structured/foreach solver
   semantics, canonical solve-before scheduling, transactional object
   randomization, staged scope `std::randomize`, randomize callbacks, focused
   frontend/application/runtime tests, and these two v2 records.
3. Batch 149 is complete as one accumulated Changes 2-20 implementation commit
   after the user-requested Change 1 checkpoint. Do not recreate or discard
   that history when starting Batch 150.
4. The committed implementation patch for the global `/bigobj` correction has SHA-256
   `380a29f6b83cda3d19c97ecaddaff1b44f177baba71bd22e8598eeb71968b7c7` when
   `git show --format= -- CMakeLists.txt cmake/CheckMsvcDebugContract.cmake` is
   piped to `sha256sum`.
5. Batch 150 is expanded into exactly 20 numbered changes without changing its
   locked allocation. Change 1 is complete: focused scalar metadata owns exact
   shortreal/real/realtime/time identities and canonical decimal/time literals;
   declarations, ports, callables, class properties, typedefs, and type actuals
   parse them with complete spans and stable malformed exponent/unit
   diagnostics. Change 2 is complete: the central scalar service propagates
   exact kinds and folds locale-independent IEC 559 real/time arithmetic,
   comparisons, conditions, casts, conversions, truth, and mixed integral
   operands with checked pure-integral overflow. Change 3 is complete: a typed
   scalar environment preserves real/time parameters, localparams, imported
   package values, dependent defaults, canonical specialization identities,
   child overrides, exact time contexts, and declaration-order dependency
   rejection independently of four-state integral constants. Change 4 is
   complete: exact scalar identities and net/variable provenance cross module
   ports, callable arguments/returns/defaults/directions/ref profiles, and
   full-profile interface export matching. Change 5 is complete: an
   engine-neutral canonical binary32/binary64/tick arena and checked shared
   arithmetic kernel enforce value/byte/work budgets, finite results, exact
   tick bounds, and nearest-rounding host admission. Change 6 is complete:
   exact comparisons, finite truth, checked signed/time/real conversions, four
   rounding modes, source-format IEEE classification, X/Z rejection, and
   destination overflow checks share that kernel. Change 7 is complete: one
   bounded locale-independent scalar text service owns display/scan/text-file
   conversion; exact time contexts own declared-precision delay scaling,
   checked target scheduling, and `$time`/`$stime`/`$realtime` behavior. Change
   8 is complete: typed SimIR/application transport, interpreter and compiled
   O0/O2 signal loads/stores, debugger mutation/inspection, independent
   callbacks, snapshots, and scalar-aware VCD preserve canonical raw payloads;
   real-family values use VCD real declarations while exact `time` remains a
   64-bit vector. Change 9 is complete: the nonnumeric opaque `chandle` type is
   retained through declarations, typedefs, parameters, ports, callables, and
   class/procedural scopes; contextual null, assignment, cast, equality, and
   inequality resolution is exact, while numeric use rejects through stable
   diagnostics. Change 10 is complete: the simulation-owned registry issues
   generation-qualified opaque identities, validates null/live/stale alias
   transfer, owns one-shot cleanup and resource limits, and exposes pointer-free
   callbacks, debugger views, snapshots, and exact-vector traces through typed
   interpreter and compiled O0/O2 application paths. Change 11 is complete:
   strict UTF-8 storage and one shared Unicode-scalar iterator now own length,
   indexing, inclusive slicing, indexed assignment, comparison, mutation, and
   conversion across interpreter and native O0/O2 execution, with malformed
   input and expanding replacements rejected transactionally. Change 12 is
   complete: the full standard string method set uses code-point-aware ASCII
   case/compare/substring behavior, deterministic radix conversion, and the
   shared locale-independent scalar scanner/formatter for `atoreal`/`realtoa`,
   with source interpreter/O0/O2 and cache differentials. Change 13 is complete:
   recursive container profiles and value-owned storage now cover scalar,
   string, nested-container, and heterogeneous unpacked-aggregate elements;
   real/time/chandle assignment patterns plus recursive default/resize/copy/
   comparison, debugger, object, and cache paths pass focused gates. Change 14
   is complete: recursive scalar/string constant substitution now reaches
   imported and local callables, typed runtime scalar arithmetic/comparison
   executes through interpreter and native paths, static/automatic locals and
   four-family task copy-out are covered, and two independently specialized
   roots retain isolated parameter values and static state. Bounded recursive
   call graphs continue to reject through the stable function/task diagnostics.
   Change 15 is complete: bounded scalar text/binary file I/O, formatting,
   scanning, descriptor behavior, and transactional copy-out now pass focused
   interpreter/compiled and artifact/cache differentials. Change 16 is
   complete: owning-unit schema 8 and portable-library schema 5 retain exact
   scalar expression/type fields, deterministic portable round trips cover all
   five scalar identities and negative decimal payloads, and context-typed
   unary constants survive object reload. The artifact matrix passes object-to-
   design, standalone trace, relocated mapped-library, interpreter/LLVM O0/O2
   cold/warm, and edited-source native-key differentials. Focused library/
   object/design artifact, application, catalog, and 559-file source gates
   pass. Change 17 is complete: stable parse/semantic/elaboration codes cover
   malformed literals, incompatible profiles/conversions, and unsupported
   operators; runtime proof covers overflow/resources, invalid Unicode,
   stale/null handles, and rollback; both scalar-aware artifact codecs reject
   invalid enumeration state before publication. Change 18 is complete: direct
   canonical payload/callback comparison spans interpreter and LLVM O0/O2,
   scheduling and VCD match across cold/warm caches, and the combined debugger,
   trace, multiple-root, string/file/aggregate, artifact/relocation, and edited-
   cache positive gates pass. Change 19 is complete: architecture and language
   support now own the scalar/Unicode/chandle/artifact model and the explicit
   Batches 151-162 UVM-readiness boundary; diagnostics cover malformed scalar
   artifact enumerations; feature rows `SV-723` through `SV-732` remove the
   completed real/chandle/Unicode surface from the deferred data-model row.
   The reviewed baselines are 1,876 diagnostics, 559 bounded sources, 649
   SPDX-owned artifacts, 217 test/control files, 1,162 executable rows, 4,648
   evidence cells, 409 exact paths, and 110 runtime owners. Inventory and
   release-candidate gates pass. Change 20 local qualification is complete:
   LLVM-disabled ASan/UBSan passes 109/109 in 760.62 seconds with leak detection
   disabled under the managed ptrace runner, exact LLVM 22.1.8 Debug passes
   112/112 in 349.47 seconds, and Release passes 112/112 in 304.22 seconds. The
   boundary repairs LLVM-disabled class-cache expectations and a GCC 13 `-O3`
   false-positive move warning for synthesized ports with no recursive delay.
   Preserve the accumulated dirty worktree until the single Batch 150 commit,
   then push and inspect/repair every non-documentation hosted CI job.
6. Batch 149 Changes 1 through 20 are complete. Exact LLVM 22.1.8 Debug passes 112/112 in
   139.03 seconds and Release passes 112/112 in 114.29 seconds after
   eight-worker builds. Source, catalog, inventory, installed-public-contract,
   Windows ABI/plan, legality, differential, resource, and release-candidate
   gates are green. Batch 149 is not a monitoring boundary, so no sanitizer or
   hosted CI inspection was run. Resume by expanding Batch 150's locked compact
   allocation into 20 numbered changes without altering scope or priority.
7. Change 2 defines owning canonical class/property/constraint HIR with base
   ownership, exact qualifiers, source spans, and normalized expression trees.
   Change 3 attaches typed canonical bindings for every exact specialization,
   including derived/base selections, local-access properties, parameters, and
   method profiles; the focused proof distinguishes parameter values 3 and 7.
   Change 4 adds deterministic base-order block composition, exact override
   provenance/legality, and enabled/disabled default mode state.
   Change 5 carries `rand`/`randc` through specialization and class-state
   schema 4 into independently owned per-object runtime profiles with checked
   signedness, width, nominal enum/handle identity, revisions, and storage.
   Change 6 derives host-independent root, per-root object-ordinal, property,
   and call-site/invocation-ordinal streams from the project seed. Source
   allocation carries its root process identity through all three engines;
   runtime and application proofs establish equal-seed replay and deliberate
   divergence for changed seed, root, object, or call identity.
   Change 7 adds an iterative value-semantic finite-domain solver over exact
   bit-vector/integer/enum profiles. Variable, clause, aggregate-domain,
   search-step, and elapsed-work limits are explicit; registration and solve
   exhaustion expose typed reasons and never publish partial state.
   Change 8 lowers exact-specialization HIR into validated source-order solver
   graphs for equality, relations, arithmetic, bitwise/logical, unary, and
   conditional operators. Width/sign extension and truncation are explicit;
   four-state controlling/merge behavior is retained and an unknown final
   predicate rejects. The `MAX=3` source graph solves against canonical
   property variables in the focused semantic-HIR proof.
   Change 9 retains and lowers source `soft`, `inside`, `dist :=`, and
   `dist :/`. Checked rational weights produce replay-driven deterministic
   domain permutations; hard clauses dominate later-priority soft clauses,
   distribution conflicts become unsatisfiable, and weight overflow rejects
   transactionally.
   Change 10 retains and lowers implication, structured blocks, if/else, and
   foreach with exact iterator-local bindings. Materialized container elements
   become typed variables; constant/iterator bounds are checked and a focused
   runtime proof traverses 300 elements with only explicit solver budgets.
   Change 11 retains and lowers source solve-before lists. Transactional cycle
   detection precedes canonical-identity topological ordering; declaration IDs
   no longer influence search or weighted replay ordering.
   Change 12 adds one commit-or-zero object randomization transaction over
   selected complete domains and fixed current-value domains. Class and inline
   constraint factories share the solver and call-local replay stream; complete
   assignment and revision validation precede publication. Source
   `randomize()` and optional property lists execute through the class-call
   service against live owning constraint HIR. Runtime proof covers inline
   composition, replay, rollback, and resource exhaustion; the application
   matrix composes an `inside` class block for selected and full calls.
   Change 13 is complete. Its runtime scope transaction jointly supports
   complete integral, explicit enum, and materialized container-element domains,
   inline solver configuration, staged publication, deterministic selection,
   and zero-result rollback. Source `std::randomize` lowers packed local
   integrals/enums to one SimIR operation, consumes the process-local replay
   stream, and publishes through ordinary copy-out. LLVM validation selects the
   interpreter solver fallback until Change 18. Focused frontend, LLVM,
   application, runtime, and source-line gates pass.
   Change 14 runs the most-derived inherited zero-argument void pre/post hooks,
   suppresses post after a zero solver result, and contains callback failure as
   zero. Pre failure restores its writes and post failure restores the complete
   pre-call property/revision snapshot. Inherited constraint bindings now cover
   every exact derived specialization. The application matrix proves override,
   unsatisfiable, pre-failure, and post-failure behavior.
   Change 15 resolves property `rand_mode` and constraint-block
   `constraint_mode` queries/updates to canonical members with exact public,
   protected-derived, and local-owner access. Every object owns independent
   property and constraint modes; disabled variables and blocks are omitted
   from the shared solve. Class-state schema 5 carries the composed mode
   inventory through standalone artifact reload. Runtime, frontend, and source
   proofs cover isolation, invalid selections, visibility, disable/re-enable,
   revisions, and satisfiable-versus-restored-unsatisfiable behavior.
   Change 16 adds deterministic exact-domain `randc` permutations with portable
   signatures, cycle ordinals, and sparse used-value indices. Exact-domain
   changes invalidate incompatible state; explicit reset and object reseed have
   deterministic replay rules; sparse state is storage-accounted and staged
   transactionally. Runtime proof covers full/constrained cycles, domain
   changes, reset/reseed, accounting, and rollback, while the source matrix
   completes the constrained 1..3 cycle in every engine and artifact path.
   Change 17 catalogs and proves malformed constraint syntax, invalid object and
   scope randomization selections, exact mode access, unsatisfiable/resource
   rollback, callback containment, null/stale handles, and corrupt solver
   clauses. It also fixes `std::randomize` being mistaken for a user package.
   Change 18 adds required checksummed constraint-HIR schema 1 to standalone
   designs, restores it after relocation, and validates its class/constraint
   graph. Object/mapped-library rebuilds retain the same owning projection.
   Stable debugger/callback/trace snapshots expose modes, revisions, seeds,
   domain signatures, cycle ordinals, and used counts. `ScopeRandomize` remains
   a safe interpreter fallback but its full shape now participates in native
   cache identity.
   Change 19 synchronizes the public architecture and language contract, stable
   diagnostics, feature rows `SV-713` through `SV-722`, clean-room inventory,
   differential/release evidence, the UVM boundary, and this restart record.
   The reviewed baselines are 1,870 diagnostics, 537 bounded sources, 627
   SPDX-owned artifacts, 212 test/control files, 1,152 executable rows, 4,608
   evidence cells, 389 exact paths, and 108 runtime owners. Catalog, source,
   legality, SystemVerilog, inventory, differential, resource, and release-
   candidate gates pass.
   Change 20 completes exact-LLVM Debug and Release qualification at 112/112 in
   139.03 and 114.29 seconds. Both configurations include source, catalog,
   inventory, installed-public-contract, Windows ABI/plan, legality,
   differential, resource, and release-candidate gates. The batch closes with
   one accumulated commit and push; no sanitizer or hosted CI inspection is
   run because Batch 149 is not a monitoring boundary.
   The current exact-LLVM Debug evidence is eight-worker focused builds plus
   passing `fsim.frontend`, `fsim.application.systemverilog_hir`,
   `fsim.runtime`, `fsim.application`, `fsim.library.artifact`, and
   `fsim.source-line-budget` tests. `git diff --check` is clean.
8. Use `cmake --build build/llvm22-ninja-debug --parallel 8` or another local
   build with at least eight workers. Batch 150 is a CI-monitoring boundary,
   but its sanitizer and hosted inspection remain Change 20 work. GitHub
   Actions stays at four-way parallelism.

The complete remaining release roadmap is locked through Batch 175:

- 149: class constraint solving and randomization.
- 150-155: real/string/foreign scalars, arbitrary-width and unpacked data,
  procedural/program/clocking/interface closure, SVA, and functional coverage.
- 156-158: DPI-C, VPI, and VHPI.
- 159-162: UVM object/factory/config/reporting, phases/objections/TLM,
  sequences/register model, and UVM 1.2/2020-3.1 conformance.
- 163-165: residual VHDL-2008/PSL, Verilog-2005, and SystemVerilog-2017 closure.
- 166-167: older VHDL, Verilog, and SystemVerilog standard modes.
- 168-170: SDF 4.0 parsing plus Verilog/SystemVerilog/VHDL/VITAL/mixed
  annotation with SDF 2.1/3.0 input compatibility.
- 171: deterministic FST tracing.
- 172-175: ABI/artifact/migration freeze, cross-platform qualification,
  release-candidate packaging/documentation, and final `v2.0.0` qualification.

Batches 150, 160, and 170 are the only remaining CI-monitoring boundaries.
Only their Change 20 runs the LLVM-disabled sanitizer immediately before the
single commit, then pushes and monitors/repairs all non-documentation GitHub
Actions jobs. All other batches run no sanitizer and no hosted CI inspection.
The v2 language-closure boundary is the standardized digital surface recorded
in the official plan; VHDL-AMS, proprietary semantics, full Accellera SystemC
kernel/TLM/AMS/CCI compatibility, GUI/reverse/parallel simulation, standalone
AOT, and Python/notebook product work remain outside v2 unless the user changes
scope.

- Branch: `codex/v2`, tracking `origin/codex/v2`.
- Baseline: `1462f18`; annotated `v1.0.0` points to `6450599`.
- Current unit: Batch 150, exactly 20 expanded changes, in progress after pushed
  Batch 149 closeout `6279f0b`. Change 1 is complete: exact scalar identities,
  canonical decimal/time literal payloads, declaration/port/callable/class/type
  parsing, spans, and stable malformed literal diagnostics pass the frontend,
  semantic-HIR, catalog, and 538-file source gates after an eight-worker Debug
  build. Change 2 adds centralized kind propagation and deterministic IEC 559
  constant folding/conversion with exact time scaling and checked integral
  arithmetic; frontend/catalog and 540-file source gates pass. Change 3 adds a
  separate typed scalar environment for dependent module/package constants,
  canonical IEEE/tick specialization identity, imported values, child
  overrides, and checked declaration ordering; elaboration/catalog and
  542-file source gates pass. Change 4 preserves explicit net/variable scalar
  ports and exact module/interface callable profiles with full
  kind/direction/ref/default matching; frontend/elaboration/semantic-HIR,
  catalog, and 542-file source gates pass. Change 5 adds canonical
  binary32/binary64/tick storage, checked shared arithmetic, and bounded
  value/byte/operation materialization; runtime/catalog and 544-file source
  gates pass. Change 6 adds exact comparison/truth, checked packed and scalar
  conversions, four rounding modes, source-format IEEE classification, and
  explicit unknown/overflow results; runtime/catalog and 544-file source gates
  pass. Change 7 adds bounded locale-independent real/time formatting and
  scanning, exact integer text beyond 2^53, declared-precision delay scaling,
  checked event targets, and `$time`/`$stime`/`$realtime`; runtime/catalog and
  546-file source gates pass. Change 8 adds typed interpreter/application
  transport, O0/O2 and source-level signal-load/store differentials, debugger
  mutation/inspection, callbacks, snapshots, and scalar-aware VCD; runtime,
  LLVM, application-time, catalog, and 548-file source gates pass. Change 9
  adds nonnumeric opaque `chandle` ownership through declarations, typedefs,
  parameters, ports, callable profiles, contextual null/assignment/cast, and
  equality/inequality, with stable rejection of numeric use; frontend,
  elaboration, SystemVerilog-HIR application, catalog, and 549-file source
  gates pass. Change 10 adds a generation-safe simulation registry with
  pointer-free identity, transactional creation, alias/stale validation,
  one-shot cleanup, bounded callbacks, typed interpreter/application transport,
  debugger mutation and inspection, and exact-vector tracing; runtime,
  application-time, catalog, and 552-file source gates pass. Change 11 replaces
  byte indexing with one strict UTF-8/Unicode-scalar service used by length,
  iteration, indexing, slicing, assignment, comparison, methods, and numeric
  conversion. Interpreter, LLVM O0/O2, mutable-string cold/warm/edit,
  catalog, and 555-file source gates pass. Change 12 completes the standard
  string method set, including code-point-aware ASCII case/compare/substring,
  deterministic integer conversions, `atoreal`/`realtoa` through the shared
  scalar text service, and source/cache parity across interpreter and LLVM
  O0/O2; frontend, elaboration, runtime, LLVM, application, catalog, and source
  gates pass. Change 13 completes recursive scalar/string/container/aggregate
  element profiles, owned default/resize/copy/conditional/comparison behavior,
  real/time/chandle assignment patterns, exact recursive debugger output,
  object/artifact/cache transport, and explicit rejection of packed-only
  operations on composite elements. Frontend, elaboration, dedicated composite-
  container elaboration, runtime, LLVM, time/string/aggregate application,
  catalog, and 559-file source gates pass after eight-worker Debug builds;
  `git diff --check` is clean. Change 14 completes package/import and recursive
  callable constant substitution, exact scalar binary execution, full-width
  canonical casts, static/automatic local behavior, four-family task copy-out,
  stable recursive-call rejection, and isolated multi-root parameterized
  specializations. Frontend, elaboration, runtime, LLVM, time/string/aggregate
  application, catalog, and 559-file source gates pass after eight-worker Debug
  builds; `git diff --check` is clean. Change 15 completes bounded scalar text/
  binary file I/O, descriptor errors, scanning/formatting, null-only chandle
  input, transactional copy-out, schema/cache invalidation, and interpreter/
  compiled artifact parity. Change 16 completes owning-unit schema 8,
  portable-library schema 5, exact scalar expression/type round trips,
  context-typed negative scalar constants after object reload, standalone and
  mapped-library relocation, and interpreter/LLVM O0/O2 cold/warm/edit cache
  parity. Focused library/object/design artifact, application, catalog, and
  source gates pass. Change 17 completes the cataloged parse/type/profile/
  conversion and unsupported-operator matrix, runtime overflow/resource/
  invalid-Unicode/stale-null negatives, explicit transactional rollback, and
  malformed scalar-enum rejection in both artifact codecs. Change 18 completes
  direct canonical payload and callback comparison across interpreter and LLVM
  O0/O2 plus scheduling, debugger, trace, multiple-root, string/file/aggregate,
  artifact/relocation, cold/warm, and edited-cache positive differentials; its
  combined focused gate passes 9/9. Change 19 synchronizes architecture,
  language support, artifact diagnostics, feature rows `SV-723` through
  `SV-732`, deferred boundaries, inventories, UVM readiness, and this restart
  record. Its reviewed baselines are 1,876 diagnostics, 559 bounded sources,
  649 SPDX-owned artifacts, 217 test/control files, 1,162 executable rows,
  4,648 evidence cells, 409 exact paths, and 110 runtime owners; inventory and
  release-candidate gates pass. Changes 1-19 accumulate in one recoverable
  worktree. Change 20 local qualification passes LLVM-disabled ASan/UBSan
  109/109 in 760.62 seconds and exact LLVM 22.1.8 Debug/Release 112/112 in
  349.47/304.22 seconds after repairing LLVM-disabled cache expectations and a
  GCC 13 Release warning in synthesized no-delay ports. The accumulated commit,
  push, and hosted non-documentation CI inspection remain.
- Completed unit: Batch 149, exactly 20 changes, complete after pushed Batch
  148 closeout `dce6c36`. Its authoritative SystemVerilog constraint-solving
  and randomization contract is recorded in `implementation_plan_v2.md`.
  Changes 1 through 20 are complete. The root CMake
  configuration now applies `/bigobj` to every target using the MSVC
  command-line frontend,
  including clang-cl, and the former application-only option is removed. The
  MSVC Debug contract requires the directory-wide policy. Exact-LLVM Debug
  configures cleanly, the `fsim_application` target is current after an
  eight-worker build, and source-line, MSVC Debug/Release, and Windows LLVM
  contract gates pass. The accumulated worktree adds flattened owning
  semantic HIR for canonical classes, base ownership, qualified `rand`/`randc`
  properties, source-spanned constraint expression trees, and per-
  specialization typed bindings for properties, parameters, `this`/`super`,
  qualified selections, and methods. Base-order composed block views retain
  override identities, static-compatibility legality, and deterministic
  enabled state for later per-object modes. Class resolution also canonicalizes
  constraint identities with methods. Exact-LLVM Debug frontend, semantic-HIR,
  runtime, core application, artifact, and source-line tests pass after
  eight-worker builds. Per-object random state is generation-safe,
  storage-accounted, schema-4 portable, and retains exact random kind, width,
  signedness, and nominal profile without host pointers. Host-independent
  project-seed derivation now supplies stable root, per-root object-ordinal,
  property, and call-site/invocation-ordinal streams, with identical replay
  across interpreter, compiled, and debugger execution. The runtime now also
  owns a deterministic iterative finite-domain solver with exact typed
  profiles, checked registration, explicit variable/clause/domain/search/time
  budgets, and transactional satisfied/unsatisfiable/exhausted results. Exact
  specialization HIR now lowers to iterative typed expression graphs with
  signed/arbitrary-width arithmetic and relations, logical/bitwise/unary and
  conditional operators, and explicit four-state predicate legality.
  Source `inside`, `soft`, and both distribution weight forms now lower for an
  exact specialization with checked rational normalization, deterministic
  replay selection, hard/soft priority, conflict, and overflow behavior.
  Structured implication, if/else, and bounded foreach graphs now lower with
  exact iterator bindings and materialized element selection without an
  arbitrary element-count cap.
  Source solve-before directives now create checked topological edges with
  canonical tie breaking and transactional canonical cycle diagnostics.
  Source object `randomize()` now reaches the shared finite-domain transaction,
  including optional property lists, enabled class blocks, caller-supplied
  inline graphs, deterministic replay, and commit-or-zero assignment/revision
  semantics. Live project builds retain owning constraint HIR for execution;
  its portable artifact boundary remains scheduled for Change 18.
  Change 13 adds a staged scope-randomize transaction, a process-seeded SimIR
  `std::randomize` operation for local packed integral/enum values, and focused
  integral/enum/container proof. Compiled configurations use the intentional
  interpreter fallback until Change 18 owns the solver boundary.
  Change 14 adds inherited most-derived pre/post callbacks, void source-function
  completion, zero-result suppression, and complete callback-failure rollback.
  Base-owned constraints now bind every derived exact specialization.
  Change 15 adds canonical property/constraint mode query and update services,
  per-object state, exact visibility validation, solver filtering, and portable
  class-state schema 5 mode inventory. Direct and artifact-reloaded source
  execution plus runtime/frontend visibility and isolation proofs pass.
  Change 16 adds host-independent exact-domain `randc` cycles, domain-change
  invalidation, reset/reseed replay, sparse heap accounting, and transactional
  cycle publication. Runtime and direct/artifact source cycle proofs pass.
  Change 17 completes the cataloged parse/resolution/unsupported, budget,
  callback, stale/null, and solver-corruption negative matrix with rollback.
  Change 18 persists and validates constraint HIR schema 1 through standalone,
  relocation, object/library, and cache paths and exposes portable randomization
  provenance to debugger, callbacks, and trace inspection.
  Change 19 synchronizes architecture, language support, diagnostics, feature
  rows `SV-713` through `SV-722`, inventories, the executable-randomization/UVM
  boundary, differential and release evidence, and restart state. The reviewed
  baselines are 1,870 diagnostics, 537 bounded sources, 627 SPDX-owned
  artifacts, 212 test/control files, 1,152 execute rows, 4,608 evidence cells,
  389 exact paths, and 108 runtime owners. The focused catalog, source,
  legality, SystemVerilog, inventory, differential, resource, and release-
  candidate gates pass.
  Exact LLVM 22.1.8 Debug and Release pass 112/112 in 139.03 and 114.29
  seconds after eight-worker builds. Their full suites include source, catalog,
  inventory, installed-public-contract, Windows ABI/plan, legality,
  differential, resource, and release-candidate gates. The batch closes with
  one accumulated Changes 2-20 commit and push. No sanitizer or hosted CI
  inspection is run because Batch 149 is not a monitoring boundary.
  Batch 150 is the next locked unit and must be expanded into 20 numbered
  changes before implementation begins.
- Current unit: Batch 148, exactly 20 changes, complete after pushed Batch
  147 closeout `37fbaaa`. Its authoritative source-executable SystemVerilog
  class contract and per-change status are recorded in
  `implementation_plan_v2.md`. Changes 1-20 are complete. Checked type
  environments now resolve module/process/function/task/
  block/argument/return class handles to canonical lexical, package, or
  compilation-unit identities. Ordinary design units retain a stable
  compilation-unit identity through portable owning-unit schema 6, and
  module-scope handles migrate transactionally from unresolved signals to
  typed variables. Focused frontend, portable-artifact round-trip, and source
  budget tests pass after eight-worker builds. The parser now gives source
  `new`, `null`, and `$cast` distinct owning expression markers, preserves
  positional/named constructor and method actuals with the explicit receiver,
  and retains handle assignment/equality, property selections, class-qualified
  statics, and task calls. Focused frontend and source-budget gates remain
  green. A separate class-expression resolver now walks class/module/process/
  function/task/block scopes, propagates expected class types through
  assignments and returns, and binds constructors, casts, instance/static
  properties, functions, tasks, `this`, and `super` base constructors to
  canonical identities with explicit receivers before lowering. The full
  frontend suite and `fsim.application` pass after eight-worker builds.
  Owning SystemVerilog semantic HIR now distinguishes class handles and null,
  allocation, checked cast, instance/static property, and instance/static
  method operations; carries canonical class/member identities and checked
  access metadata; and marks typed assignment/return transfers without host
  pointers. A direct HIR regression covers the complete pre-lowering surface.
  Source `new(...)` now lowers to an owning SimIR allocation carrying aligned
  packed actual registers and names. Specialized method profiles retain
  constructor formals, defaults, locals, and bodies through class-state schema
  2. The runtime associates positional/named/default actuals, executes explicit
  or implicit base construction before the derived body, and initializes
  owner-qualified hidden properties. The exact Debug core application case
  passes across interpreter, compiled-fallback, debugger, class-state and
  standalone design-artifact paths; its source-created derived object has both
  base and derived `value` members initialized to 3.
  Instance property reads/writes and class function calls are now owning SimIR
  operations. Resolved calls retain operand-aligned directions and packed
  return profiles through portable owning schema 7. The source evaluator owns
  implicit `this`, positional/named/default association, automatic and
  static-lifetime locals, input/output/inout/ref copy rules, returns, guarded
  recursion, explicit `super` dispatch, and owner-qualified hidden properties.
  The exact Debug core application and full frontend suites pass; the class
  case proves all four formal modes, persistent locals, legal recursion, base
  method access, property reads, and producer-independent artifact execution
  across interpreter, compiled-fallback, and debugger engines.
  Resolved class-task calls now retain source formals, locals, and bodies so
  elaboration can synthesize ordinary automatic task frames on the common
  SimIR call stack. Delay, edge-wait, assertion, register lifetime, resume, and
  copy-out semantics use the existing scheduler. Runtime-state schema 6 gives
  class operation results explicit widths before LLVM validation chooses
  interpreter fallback. The exact Debug core case proves a local across delay,
  delayed output/inout copy-out, assertion execution, and a nested-class task
  waiting on a module signal edge across all engines and artifacts.
  Ordinary and explicit `super` calls now carry distinct owning markers, and
  SimIR records whether each source call requests virtual dispatch. The runtime
  resolves the declaration profile, selects the matching stable slot on the
  heap object's dynamic specialization chain, verifies exact profile identity,
  and rejects pure selections. The exact Debug proof calls a derived override
  through an `AppBase` handle while `super.bump` remains nonvirtual and updates
  only the hidden base property; all engines and artifacts remain green.
  Source static properties and methods now use owning SimIR operations backed
  by the shared per-specialization store. Static functions execute directly;
  static tasks reuse synthesized scheduler frames for delay and copy-out; and
  inherited aliases select the base declaration's one state object. Reads
  normalize internal integer storage to the declared executable width. The
  exact Debug class proof checks base-first initialization, function updates,
  delayed task update, inherited static selection, all engines, and standalone
  artifacts. Focused owning-HIR, frontend, portable-artifact, and application
  tests pass after eight-worker builds. Source function returns and suspending
  task input/output now preserve opaque handle aliases. Instance/static handle
  properties use dedicated typed slots with dynamic-view validation, and
  fixed/dynamic/queue/associative class-property containers execute indexed
  reads/writes, dynamic sizing, and queue push/size/pop without imposing an
  arbitrary element count. Existing runtime aggregate coverage remains green
  for named unpacked handle members and bounded storage accounting. Exact
  Debug runtime, owning-HIR, frontend, portable-artifact, and application
  proofs pass after eight-worker builds. Class resolution and generate
  expansion now preserve typed class variables and their qualified references
  inside generated bodies. A final process observes its live source object;
  parameter-specialized generated leaves beneath wrapper modules construct two
  distinct objects in aliased roots while sharing one scheduler and static
  store. Interpreter, compiled, and debug runs agree on time, root values,
  heap count, and shared state. Native O0/O2 execution now returns an
  append-only SimIR service-boundary status for class operations; the common
  scheduler executes the typed heap/static hooks and immediately resumes the
  generated frame. Native-cache schema 81 hashes only canonical identities,
  registers, actuals, directions, dispatch, and widths. Direct C ABI and LLVM
  O0/O2 tests prove instruction, PC, and register handoff, while the exact
  Debug application core remains green across every engine and standalone
  artifacts. Source-created objects now have deterministic declared/dynamic
  debugger views; canonical static-state and suspended-call snapshots expose
  packed properties, frames, and call identities without internal addresses.
  Source construction and instance/static operations publish packed time/delta
  callbacks visible at scheduler safe points, and deterministic packed class
  snapshots feed the existing VCD writer. Exact Debug runtime, LLVM, and core
  application proofs pass after eight-worker builds. Artifact execution found
  and closed a missing class-call direction/result serialization defect;
  class-state schema 3 now owns the complete executable expression profile.
  Copied read-only `.fsimdesign` and relocated mapped `.fsimlib` payloads run
  with source and `.fsimobj` hidden while retaining class operations, SimIR
  continuations, method bodies/provenance, initial handles/statics, and final
  results. Cold/warm/semantic-edit native-cache evidence passes. The ordinary
  module fixture additionally proves checked success/failure `$cast` with
  destination preservation plus class handles through an automatic module
  function and suspending module task across every engine/artifact. An explicit
  engine snapshot now proves identical time, hidden properties, shared state,
  live-object count, and packed trace inventory for interpreter, compiled/O2,
  and debug/O0, alongside the direct O0/O2 boundary, callbacks, multi-root,
  relocation, and cache evidence. All class resolution/lowering diagnostics
  are now cataloged. Malformed class-call native HIR publishes no symbol;
  truncated/future/trailing class-state payloads reject; failed `$cast`
  preserves its destination; and the accumulated frontend/runtime negatives
  cover transactional access/profile/construction, pure/null/stale/suspension,
  recursion, cycles, and exact resource budgets without arbitrary container
  caps. Change 19 is complete: architecture, language support, diagnostics,
  feature evidence, the class/UVM boundary, and restart records describe the
  source-executable slice without claiming constraint solving or UVM closure.
  Class inspection, container type construction, class JIT validation, and
  SimIR debug metadata now have focused structural owners. The reviewed
  inventory is 1,850 diagnostics, 526 bounded sources, 616 SPDX-owned
  artifacts, and 211 test/control files; the release evidence covers 1,142
  executable rows, 4,568 evidence cells, and 377 exact paths. Source, catalog,
  inventory, legality, differential, and release-candidate gates pass. Change
  20 is complete. The first full Debug pass found that ordinary interface-
  function lowering removed a receiver operand without its newly aligned
  named-argument metadata; both packed and container-return paths now remove
  the pair transactionally, and the interface regression passes in both
  configurations. Exact LLVM 22.1.8 Debug passes 112/112 in 331.12 seconds and
  Release passes 112/112 in 289.34 seconds after eight-worker builds. All
  source, catalog, inventory, installed-public-contract, Windows ABI/plan,
  differential, legality, and release-candidate gates are green. The batch is
  ready for its single commit and push; no sanitizer or hosted CI inspection
  was run because Batch 148 is not a monitoring boundary.
  Changes 1-19 remain one recoverable accumulated worktree; Change 20 alone
  owns full gates, one commit, and one push. Batch 148 is not a CI-monitoring
  batch; do not run a sanitizer or inspect hosted CI. The batch closes the
  source-to-runtime seam for typed class objects, `new`, constructors,
  properties, instance/static/virtual functions, suspending tasks, handle
  containers, debugger/callback/trace visibility, artifacts, and native-cache
  identity. Constraint solving/randomization and UVM behavior remain assigned
  to subsequent batches.
- Current unit: Batch 147, exactly 20 changes, complete after pushed Batch
  146 closeout `eafadad`. Its authoritative SystemVerilog class object-model
  contract and per-change status are recorded in `implementation_plan_v2.md`.
  Changes 1-20 are complete. The owning HIR and focused
  frontend proof retain compilation-unit, package, module, interface, and
  nested class declarations, forward-definition merging, lifetime and class
  kinds, parameterized base selections, closing names, canonical lexical
  identities, and duplicate diagnostics without adding a top-selectable unit
  kind. Declaration-ordered properties retain visibility, static/const/random
  qualifiers, strings, multidimensional containers, aggregate typedefs, and
  class-handle spellings. Constructors, instance/static/virtual methods,
  `this`/`super` selected names, pure and extern prototypes, defaults,
  constraints, and qualified out-of-block definitions are also source-owned
  and warning-clean in the focused frontend test. The project merge now
  preserves compilation-unit classes and out-of-block definitions with their
  source order, logical library, and compilation-unit digest. A central
  case-sensitive resolver assigns canonical lexical/package identities,
  resolves nested, imported, package-qualified, and compilation-unit class
  handles and bases, completes matching extern definitions transactionally,
  and diagnoses incomplete forwards, missing/ambiguous bases, ambiguous handle
  types, and invalid out-of-block ownership. Default and referenced value/type
  parameterizations now materialize deterministic identities, inherited
  specializations, finite instance/static property layouts, method profiles,
  and transitive source provenance with storage-derived overflow checks. A
  separate legality pass rejects inheritance/interface cycles, duplicate
  profiles, pure methods on concrete declarations, nonvirtual final methods,
  non-interface implementations, incompatible/static-changing/final
  overrides, and unfulfilled pure obligations while excluding local base
  members from inherited lookup. The scheduler-facing class heap uses null
  handle zero, generation-safe slot identities, deterministic lowest-slot
  reuse, declared/dynamic/specialization metadata, language-default property
  storage, and caller-supplied live/storage budgets. Transactional construction
  runs ordered base-to-derived steps; opaque handle assignment, equality,
  argument/return alias transfer, named property access, checked type views,
  cleanup, and null/stale/downcast/budget failures pass the focused runtime
  test without exposing host pointers. A resource-governed method runtime owns
  `this`, arguments, automatic locals, recursion depth, suspended task frames,
  and deterministic copy-in/copy-out; compiler-assigned stable virtual slots
  retain inherited override identity, dispatch on the heap object's dynamic
  type, preserve explicit nonvirtual base calls, and reject pure calls. A
  simulation-wide per-specialization static store initializes base state
  before derived state, resolves class and import/root aliases to one value,
  supports implicit and qualified static-method access, and enforces caller
  property/storage budgets. Opaque handles also flow through bounded fixed and
  dynamic arrays, queues, associative arrays, and unpacked aggregates embedded
  in class properties; edits preserve aliases, validate declared element
  types, reject illegal packed placement, and use caller-derived capacities.
  Checked specializations now survive into each built project; one simulation-
  wide heap, static store, and method dispatcher serves every hierarchy root.
  Source-derived static integer initialization, inherited object layout,
  deterministic scheduled virtual calls, exact time/delta packed-property
  callbacks, and opaque-object debugger inspection pass the application case
  under interpreter, LLVM compiled, and debugger engines. The added class
  lookahead also preserves parameterized module instances in generate bodies.
  Owning-unit schema 5, portable-library schema 4, runtime-state schema 5, and
  class-state schema 1 preserve compilation-unit class declarations and
  out-of-block methods in `.fsimobj`/mapped `.fsimlib`, and preserve specialized
  layouts, initializers, inherited ownership, virtual slots, and provenance in
  `.fsimdesign`. The focused artifact proof hides both producer source and
  object before standalone execution and also rebuilds from the relocated
  mapped library. Its interpreter, LLVM O2, and debugger/O0 runs agree on
  construction, base-handle aliases, hidden base/derived properties, explicit
  base versus virtual override dispatch, shared static state, scheduled
  callbacks, debugger reads, and artifact-restored state; runtime coverage adds
  task suspension and every handle-container shape.
  Transactional negative coverage now fixes the malformed-header,
  qualifier-conflict, duplicate-member/constraint, pure-method,
  out-of-block-ownership, parameter-type/value, inheritance/override,
  null/stale/downcast, heap/static/container budget, static-cycle,
  host-layout-overflow, class-state schema/trailing-byte, and artifact checksum
  diagnostics. Architecture, language-support, diagnostic, feature-matrix,
  evidence-inventory, and class/UVM boundary documentation now describe the
  implemented object-model foundation without claiming constraint solving or
  UVM closure. The catalog covers 1,829 production codes, the source gate
  covers 518 bounded files, and the reviewed inventory covers 608 SPDX-owned
  artifacts and 211 test/control files. The exact-LLVM Debug and Release
  suites pass 112/112 in 137.41 and 105.90 seconds. Their accumulated source,
  catalog, inventory, installed-public-contract, Windows ABI/plan,
  differential, legality, and release-candidate gates are green; the latter
  covers 1,137 executable rows, 4,548 evidence cells, 372 exact paths, and 108
  runtime owners. The batch remains one accumulated implementation commit and
  one push. Batch 147 is not a CI-monitoring batch and ran no sanitizer or
  hosted CI inspection. The batch establishes owning class HIR,
  parsing and name/type resolution, parameterized inheritance, a checked
  opaque-handle heap, construction and handle semantics, instance/static and
  virtual methods, container integration, hierarchy/debug/artifact/cache
  behavior, negatives, and release evidence. Constraints/randomization and
  UVM behavior remain assigned to subsequent class-closure batches.
- Batch 146, exactly 20 changes, is complete after pushed Batch
  145 closeout `9b771eb`. Its authoritative Verilog-2005 specify-timing
  contract and per-change status are recorded in `implementation_plan_v2.md`.
  Changes 1-20 are complete. The warning-clean focused
  frontend regression passes after an eight-worker build. Specify HIR retains
  source-spanned blocks, scalar/mintypmax/path-pulse specparams,
  parallel/full and edge-sensitive paths, polarity and destination data-source
  transforms, `if`/`ifnone`, one through twelve transition delays, pulse style
  and cancellation controls, all twelve Verilog-2005 timing-check forms,
  notifier and optional compound-check arguments, and explicit edge
  descriptors. Specparams specialize in declaration order after module
  parameters, feed every timing expression, reject non-static values through
  `FSIM-ELAB-SVSPEC-001`, and add state-preserving native-cache identities.
  Change 8 is complete: hierarchy validation resolves terminals through the
  instance-local signal map and rejects incompatible module-port directions,
  non-static widths, and unequal parallel paths through
  `FSIM-ELAB-SVSPEC-002` through `007`, retains packed lane selections and
  transition delays in checked design state, assigns the exact destination
  driver set after lowering, and translates the result into a scheduler-owned
  module-path arc. Change 9 adds storage-budgeted, recursion-free runtime
  expression programs for conditions and destination data, ordered conditional
  groups with `ifnone`, polarity/edge metadata, pulse style and cancellation
  policy, exact transition tables, and immutable source provenance. The focused
  frontend, elaboration, and runtime tests pass; the runtime proof selects an
  `ifnone` rise at tick 3, then an enabled data-source fall at tick 27. Change
  10 completes common-scheduler execution: exact one/two/three/six/twelve-entry
  transition selection, packed parallel lane pairing versus full-path fanout,
  intrinsic-plus-path delay accumulation with overflow checks, stable ordering,
  and inertial replacement all pass focused tests. Change 11 closes ordered
  competing `if` selection, X-valued condition fallback to `ifnone`, positive
  and negative edge filtering, polarity transforms, and destination-data
  evaluation. Change 12 adds timescale-normalized global and terminal-specific
  PATHPULSE reject/error limits, onevent and ondetect X publication,
  showcancelled negative-pulse windows, stale-free overlapping recovery,
  overflow checks, callbacks, and VCD-visible corruption. Change 13 executes
  all seven simple timing checks with exact event direction, four-state event
  conditions, persistent history, mintypmax limits, width thresholds, and
  notifier updates. Change 14 executes all five compound checks with signed
  negative-timing windows, timestamp/check conditions, delayed signal copies,
  event- and timer-based skew, remain-active behavior, and notifiers. Change
  15 passes a dedicated integration differential across interpreter, LLVM
  O0/O2 cold and warm caches, and debugger execution with parameterized
  generate leaves, aliased multiple roots, recursive SV-to-VHDL-to-SV
  hierarchy, resolved duplicate drivers, force/release, callbacks, and VCD.
  Change 16 advances owning-unit/runtime/library schemas, round-trips specify
  HIR and normalized state through `.fsimobj`, relocated `.fsimlib`, and
  relocated standalone `.fsimdesign` execution, and preserves cold/warm/edit
  native-cache behavior. Change 17 closes the positive differential matrix by
  combining exact runtime form coverage with the representative interpreter,
  LLVM O0/O2, debugger, artifact, relocation, and cache differential. Change
  18 covers malformed arities/optionals, staticness, signed constraints,
  controlled edges and descriptors, terminal/notifier/delayed widths, invalid
  portable HIR and runtime state, schema mismatch, payload over-materialization,
  and time overflow. Change 19 synchronizes architecture, public support and
  compatibility notes, diagnostics, six feature rows, release inventories,
  README, and this restart evidence while retaining SDF as dedicated later
  work. Change 20's exact-LLVM Debug and Release regressions pass 112/112 in
  136.29 and 106.68 seconds after eight-worker builds. Source, diagnostic,
  inventory, installed-public, Windows ABI, differential, and release gates
  pass at 1,777 diagnostics, 501 bounded sources, 591 SPDX-owned artifacts,
  208 test/control files, 1,127 feature rows, 4,508 evidence cells, 362 exact
  paths, and 107 runtime owners. The accumulated batch owns one commit and one
  push. Batch 146 is not a CI-monitoring batch; do not run a
  sanitizer or inspect hosted CI. The batch covers specify-block/specparam HIR,
  parallel/full and conditional module paths, every path-delay arity,
  polarity/data-source and edge forms, pulse controls, all Verilog-2005 timing
  checks, notifiers, hierarchy and artifact/cache integration, both engines,
  debugger/callback/VCD behavior, diagnostics, and release evidence. SDF
  annotation remains a later dedicated batch.
- Batch 145 is complete after pushed Batch 144 closeout `4be4a51`. Its
  authoritative Verilog-2005 strength and switch primitive contract and
  per-change status are recorded in `implementation_plan_v2.md`. Changes 1-20
  are complete. The exact-LLVM Debug, Release, and LLVM-disabled focused slices
  pass for
  frontend, elaboration, runtime, library artifacts, application resolution,
  diagnostic catalog, and source budget. Transmission-switch regression now
  proves release to `Z`, reconnection, conditional enabled/disabled/unknown
  conductance, repeated resistive reduction, and cycle-safe resolution without
  stale retention. Bidirectional transmission metadata now names source,
  target, optional control, polarity, and resistance in runtime-state and
  native-cache provenance; the scheduler resolves the connected-net graph and
  the transmission processes own no driver slots or hierarchy objects. Pull,
  supply, implicit-pull, scalar/packed charge retention, charge-strength
  arbitration, zero decay, finite decay, renewed-drive cancellation, and
  infinite retention pass the same interpreter/LLVM and runtime-state slice.
  Generated and parameter-specialized instances, aliased multiple roots,
  searched source libraries, recursive VHDL/SystemC wrappers, disconnected
  components, and cyclic transmission graphs pass through the central
  resolver. The same strength-conflict, direct/routed switch, retained-charge,
  and finite-decay design passes through `.fsimobj`, standalone
  `.fsimdesign`, and relocated mapped `.fsimlib` flows. Interpreter and
  compiled cold/warm results match; native-cache hits, misses, stores, and a
  source edit prove topology-aware separation. The complete LLVM-disabled
  warnings-as-errors rebuild and its seven focused tests pass after the
  topology refactor.
  Negative evidence rejects ambiguous strength syntax, malformed topology and
  strength ranks, incomplete or width-incompatible native edges, unsupported
  module-instance strength profiles, a switch array beyond its 256 MiB owning
  storage budget, a corrupted strength-bearing design artifact, and decay-time
  overflow. Vector-controlled transmission now selects conductance per lane.
  Architecture, language support, README, diagnostics, feature matrix,
  evidence inventory, legality, differential, and release-candidate records
  are synchronized. All 12 focused documentation/release gates pass at 1,755
  diagnostics, 489 bounded sources, 579 SPDX-owned artifacts, 204 authored
  test/control files, 1,121 executable rows, 4,484 evidence cells, 356 exact
  paths, and 104 runtime owners.
  The time application now proves a `trireg` decay literal that overflows after
  project-resolution scaling is rejected through `FSIM-TIME-0003`.
  Changes 1-19 remained one recoverable accumulated worktree. Change 20's
  eight-worker exact-LLVM Debug and Release builds pass 111/111 tests in
  135.16 and 106.30 seconds. Source, catalog, inventory,
  installed-public-contract, Windows ABI, differential, and release gates pass.
  Batch 145 is not a CI-monitoring batch; no sanitizer or hosted CI inspection
  ran.
- Batch 145 owns the common strength-aware four-state resolver; parsed drive,
  pull, and charge strengths; ordinary and tri-state gate strengths; MOS and
  resistive MOS devices; bidirectional and conditional transmission switches;
  pull/supply/implicit-pull sources; `trireg` charge retention and decay;
  generated, multiple-root, library, and mixed-language topology; portable
  artifacts and caches; both engines; debugger, callbacks, VCD; diagnostics;
  and release evidence. Specify timing and SDF remain later dedicated batches.
- Batch 144 owns Verilog-2005 combinational and sequential user-defined
  primitives: declarations, table symbols and edge descriptors, instance
  resolution and arrays, normalized specialization, per-instance state,
  inertial delays, resolved drivers, artifacts, both engines, debugger,
  callbacks, VCD, diagnostics, and release evidence. Changes 1-19 remain one
  accumulated worktree; Change 20 owns the completed full exact-LLVM
  Debug/Release gates, documentation closeout, one commit, and one push.
  Strengths/switch
  primitives and specify timing remain separate Verilog-2005 closure batches;
  SDF remains in its dedicated roadmap batch. Do not run a sanitizer or inspect
  hosted CI for Batch 144.
- The clean-room UDP frontend HIR keeps declarations separate from
  top-selectable modules and retains ordered terminals, combinational or
  sequential kind, optional initial output, level/output symbols, shorthand or
  explicit edge descriptors, table priority, timing context, closing identity,
  and source spans. Classic combinational plus ANSI `output reg` sequential
  fixtures cover initial state, `(01)`, `r`, `n`, current-state don't-care, and
  no-change output; the focused warnings-as-errors frontend build and test pass.
  The common matcher covers every level wildcard and `r`/`f`/`p`/`n`/`*` or
  explicit transition class, current state, no-change output, stable nonedges,
  and first-row selection. Cataloged parse codes 224-243 and semantic codes
  130-144 reject malformed profiles, symbols, pairs, widths, edge counts,
  empty/duplicate rows, and duplicate declarations. The central candidate
  variant now retains UDPs separately from HDL units and SystemC factories;
  project merging preserves source order and logical libraries, while focused
  elaboration covers forward, missing, ambiguous, module-collision, positional,
  and built-in-gate cases. Declaration-aware normalization retains anonymous
  and comma-separated instances, arrays with scalar/vector bridges, and
  one/two/three-value common-model delays while ordinary modules reject UDP-only
  syntax. The focused frontend, elaboration, diagnostic-catalog, and
  source-budget gates pass. Each canonical UDP now owns one immutable shared
  normalized table and SHA-256 digest exposed through elaborated-design state;
  every specialization records the same selected table provenance. Combinational
  rows lower to ordinary four-state SimIR with first-match priority, X/Z
  normalization, unmatched X, and normal driver ownership. Sequential rows use
  one sole-driver process with persistent previous-input and initialization
  signals; focused execution covers initial state, rising/negative edges,
  no-change, retention, and repeated transitions. UDP table evaluation now
  drives a hidden value through the common continuous inertial driver. Focused
  interpreter and LLVM O0/O2 cold/warm-cache execution proves `5/7/11`
  rise/fall/turnoff timing, short-pulse cancellation, zero-delay deltas,
  same-value stability, X transitions, callbacks, debugger reads, VCD, and
  scale-overflow rejection. Generated and parameter-specialized instances,
  multiple roots, searched logical libraries, mapped `.fsimlib` content, and
  mixed VHDL/SystemC wrapper paths all retain central resolver selection.
  Portable `.fsimudp` payloads now survive `.fsimobj`, relocated
  `.fsimdesign`, standalone execution after producer inputs are hidden, and
  cold/warm/edit native-cache cycles. Combinational, edge- and level-sensitive,
  delayed, generated, and resolved-driver behavior matches across interpreter,
  LLVM O0/O2, debugger, callbacks, and VCD. Negative coverage rejects malformed
  declaration and restored-state geometry, bad table digests/provenance,
  duplicate UDP object inputs, corrupt payloads, unsupported instance forms,
  excessive host materialization, and time overflow. Static instance arrays no
  longer carry the former arbitrary 64-instance ceiling; both arrays and UDP
  tables use documented 256 MiB owning-storage guards derived from their
  materialized records. Architecture, language support, diagnostics, README,
  feature rows `SV-672` through `SV-681`, and the release inventory are now
  synchronized. Focused source/catalog/IEEE/inventory gates pass at 1,735
  diagnostics, 484 bounded sources, 574 SPDX-owned artifacts, and 202
  test/control files. Change 20 is complete: exact-LLVM Debug passes 111/111
  tests in 310.43 seconds and Release passes 111/111 in 278.64 seconds after
  eight-worker builds. The release evidence contains 1,111 executable feature
  rows, 4,444 evidence cells, 351 evidence paths, and 103 runtime owners. No
  sanitizer or hosted CI inspection ran for this non-monitoring batch. Resume
  by defining the exact 20-change Batch 145 contract for Verilog-2005 strength
  and switch-primitive closure.
- Batch 143 owns clean-room `ieee.vital_memory` public metadata and execution:
  memory declaration/loading, action and violation tables, word/subword state,
  multi-port contention, vector memory timing checks, path accumulation and
  retained-output scheduling, resource-governed geometry, representative
  vendor-style cell/memory compatibility, artifacts, both engines, debugger,
  callbacks, VCD, diagnostics, and release evidence. Changes 1-19 remain one
  accumulated worktree; Change 20 alone owns full exact-LLVM Debug/Release
  gates, documentation closeout, one commit, and one push. Do not run a
  sanitizer or inspect hosted CI for this batch.
- Batch 143 now materializes the complete clean-room public
  `ieee.vital_memory` metadata and both `VitalDeclareMemory` profiles. The
  declaration runtime uses resource-governed arbitrary-width UX01 storage,
  confined hexadecimal/binary file loading, interpreter and LLVM callbacks,
  and native-cache schema v78. Direct runtime negatives plus the existing
  VITAL application differential pass after an eight-worker structural clean
  rebuild. Address/data state decoding and word/subword table lookup now cover
  all legal transition/level/flag symbols, first-row/default behavior,
  independent vector enables, short final subwords, and arbitrary-width
  corruption masks. All word/subword table actions now execute with data-before-
  memory ordering, exact UX01/Z values, current/previous per-port state, bus
  history, transitioned addresses, and stable-call output suppression; resume
  with cross-port interaction in Change 9. Both cross-port profiles and both
  violation profiles now pass focused runtime coverage for forwarding,
  contention, port disabling, deterministic pairing, sized scalar/vector
  masks, port-type gating, invalid addresses, and reporting control. Vector
  setup/hold checks now cover cross, parallel, and subword pair maps with
  per-pair state, per-entry delay/limits/enables, aggregation, and independent
  X/message selection. Vector period/pulse checks preserve per-bit state and
  thresholds with the same independent controls. Memory path initialization,
  selection, and scheduling now normalize every scalar/vector and
  single/01/01Z/01ZX profile across cross, parallel, and subword arcs. Focused
  coverage includes simultaneous shortest paths, scalar/per-bit/subword
  conditions and flags, bit/word retain corruption, mapped Z values, checked
  time overflow, null ranges, and projected transport-waveform handoff. VITAL
  memories now use contiguous storage only as a small-memory optimization;
  large logical depths retain an explicit default word and resource-bounded
  sparse materialization, so total depth is not a host-allocation limit.
  Transactional loading, sparse global corruption, a one-word width budget,
  malformed geometry, and far-address access pass focused runtime coverage;
  the VITAL package and delay application cases also pass. Declarative
  user-defined attributes are now accepted in entity, architecture, and
  generated regions for VITAL vendor compatibility. The representative
  configured cell and memory models exercise VITAL_LEVEL metadata, guarded
  timing generics, extended identifiers, synthesis pragmas, null path ranges,
  generic memory declaration, component bindings, both engines, LLVM O0/O2
  cold/warm cache, debugger, VCD, runtime-state and relocated artifacts without
  vendor-name special cases. Static VITAL memory load files are now validated
  and embedded into SimIR, hashed by native cache v78, serialized through
  runtime state and design artifacts, and consumed by interpreter and LLVM
  callbacks; dynamic paths retain confined runtime loading. The relocated
  artifact test deletes the original file before standalone execution, proving
  loaded contents no longer depend on the build tree. Focused runtime, package
  integration, vendor model, LLVM O0/O2 cold/warm cache, debugger, callback,
  VCD, `.fsimobj`, runtime-state, and relocated `.fsimdesign` evidence passes.
  Documentation, diagnostics, feature-matrix, inventory, compatibility, and
  restart records are synchronized. The focused runtime, VITAL application,
  diagnostic-catalog, source-budget, IEEE-package, and inventory gates pass.
  Exact-LLVM Debug and Release each pass 111/111 tests after eight-worker
  builds, in 133.33 and 104.39 seconds. Source, catalog, inventory,
  installed-public-contract, MSVC/Windows contract, differential, and
  release-candidate gates pass. The reviewed baselines are 1,692 diagnostics,
  479 bounded sources, 569 SPDX-owned artifacts, 200 test/control files, 1,101
  execute rows, 4,404 evidence cells, 346 evidence paths, and 100 runtime
  owners. No sanitizer or hosted CI inspection ran for this non-monitoring
  batch.
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
