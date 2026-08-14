<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v2 restart handoff

Read [implementation_plan_v2.md](implementation_plan_v2.md) first; it is the
authoritative v2 batch/status record. Preserve the completed v1 history in
`v1-resume.md`.

## Batch 169 planned restart checkpoint - 2026-08-13

1. Start in `/home/colin/projects/fsim`, read this section and the authoritative
   expanded Batch 169 allocation in `implementation_plan_v2.md`, and verify
   branch `codex/v2` is clean and synchronized at pushed Batch 168 closeout
   `625c1fb328b356b2116d0f9e3012281d7f6aa71f` plus this documentation-only
   Batch 169 planning checkpoint.
2. This checkpoint implements the required pre-batch flow. Batch 169 is
   expanded into exactly twenty changes without broadening its locked
   Verilog/SystemVerilog SDF timing-application scope. No Batch 169
   implementation file has changed; after pushing this checkpoint, resume only
   from this section and the authoritative allocation.
3. Batch 168 provides clean-room SDF 4.0 parsing, independent SDF 2.1/3.0
   adapters, exact normalization, one immutable IR, deterministic mixed-
   hierarchy resolution, whole-plan validation and portable phase/artifact/
   cache persistence. It intentionally applies no annotation to runtime timing.
4. Preserve Batch 168's closed 17-row ledger and six owned corpus inputs. The
   final SDF inventory identity is
   `5a4817053834c76fef49650d38b73957a4be5560c62d1eb0f8c72c7180340c4b`;
   the synchronized feature and VHDL/PSL closure identities are
   `ac8c85884b50854481f4eb68aa621b6b6b1ff080f00b0745fcf3046de7dba1b9`
   and
   `ae197625c75869bc8d60d4c7f66151f1196f78c22d2e566664dba62bd1bec1f0`.
5. Batch 168's fresh clean-first exact-LLVM 22.1.8 Debug and Release
   eight-worker builds complete 824/824 steps warning-free in 12:16.97 and
   10:27.27 at 5,084,240 and 2,255,404 KiB peak RSS with zero swaps.
6. Complete non-sanitized Debug and Release regressions pass 162/162 in
   11:19.96 and 10:30.22 wall time, with CTest totals of 679.95 and 630.22
   seconds, peak RSS of 3,782,884 and 3,794,928 KiB and zero swaps. The SDF
   closure passes inside them in 30.48 and 26.15 seconds; final static/release
   slices pass 13/13 in 9.14 and 8.78 seconds.
7. Batch 168 is committed once as `625c1fb` and pushed. Its retained transcripts
   pass warning/error scans and record successful exit; pinned formatting
   touched only the new `sdf_parser.cpp`, with no header or legacy-source
   formatting churn. It ran neither sanitizer nor hosted-CI inspection.
8. Batch 169 applies the validated SDF IR only to Verilog/SystemVerilog specify,
   primitive, net and timing-check behavior. VHDL/VITAL targets and any timing
   that crosses a VHDL boundary remain exclusively in Batch 170.
9. Change 1 freezes a seventeen-row application ledger assigning Changes 2-18
   one-to-one across exact value policy, atomic target plans, paths,
   interconnect/device objects, delay modes/lists, timing checks, pulse behavior,
   scheduling, drive state, reannotation, APIs, persistence, observability,
   corpora and closure.
10. Changes 2-6 select and quantize exact values once, publish only fully valid
    immutable target plans, and apply path/interconnect/device/port/MIPD plus
    absolute/increment transition-list annotations without partial mutation.
11. Changes 7-10 apply the complete supported timing-check, edge, condition,
    notifier, negative-check, pulse-filtering and retain surface while
    preserving exact event roles, limits and violation coordinates.
12. Changes 11-14 define source/SDF precedence, delay modes, interpreter/LLVM
    queue semantics, strength/switch and force/release behavior, multiple-root/
    multiple-file ordering and rollback-safe reannotation.
13. Changes 15-17 expose bounded annotation control and observability through
    CLI, Tcl, C/C++ APIs, explicit non-project phases, artifacts/caches/
    checkpoints, debugger, callbacks, trace, VPI and VCD.
14. Changes 18-19 publish clean-room timing corpora and exhaustive negative,
    resource, engine, phase, artifact and platform differentials, then close
    documentation, ledgers, matrices, inventories, counts and digests.
15. Preserve Batch 168's exact parser, resolution and portable rejection
    contracts. Do not introduce VHDL/VITAL runtime timing, approximate floating-
    point annotation, per-event hierarchy lookup, silent target widening or
    compatibility readers for superseded development schemas.
16. Accumulate Changes 1-19 in one recoverable worktree with focused dependent
    Debug validation. Release build/testing is not required before final Change
    20 batch checks. Avoid header-only formatting changes that induce long
    rebuilds; make semantic header edits only when required and format changed
    implementation sources without churning unchanged headers.
17. Change 20 alone runs fresh clean-first exact-LLVM Debug and Release builds
    with at least eight workers and 120-minute command timeouts, the complete
    non-sanitized regressions and every release gate. Retain timing/RSS/swap/log
    evidence and create the sole Batch 169 implementation commit/push only
    after all local gates pass.
18. Batch 169 is neither a sanitizer nor hosted-CI monitoring boundary. Do not
    configure sanitizer targets or inspect hosted CI. After its implementation
    commit, save and push the exact Batch 170 restart plan; Batch 170 owns both
    the sanitizer and non-documentation CI inspection boundary.
19. Do not reset, commit or push accumulated Batch 169 implementation before
    Change 20. Begin only with Change 1's registered ledger, preserve each
    completed change in the same dirty worktree, and record focused evidence in
    both authoritative documents before proceeding one change at a time.
20. Batch 169 Change 1 is complete in the intentionally dirty worktree. The
    registered 17-row `sdf_application_inventory.tsv` ledger assigns Changes
    2-18 one-to-one across exact value policy, atomic target planning, path,
    interconnect/device, delay-list/mode, primary/secondary/conditional timing
    checks, pulse/retain, precedence, scheduler, drive-state, reannotation,
    public control, persistence, observability and corpus/closure domains. All
    rows remain active and bind SDF 2.1/3.0/4.0, the eight governed Verilog/
    SystemVerilog profiles, exact planned implementation and positive/negative/
    engine/phase/artifact evidence, diagnostics and governed resource owners.
    The registered validator freezes unique IDs/closures, row order, exact
    twenty-change allocation, path classes and SHA-256 identity
    `2e0cd2d32c401440a2279e15695d0c9da04c6e8cf6268e74968e82877120ad75`.
    The exact-LLVM Debug tree regenerates without compilation; application-
    inventory, diagnostic-catalog and source-budget gates pass 3/3 in 0.66
    seconds. No runtime timing, Release qualification, sanitizer, hosted-CI
    inspection or header formatting ran. Preserve Change 1 and proceed to
    Change 2 without reset, commit or push.
21. Batch 169 Change 2 is complete in the same intentionally dirty worktree. A
    typed `SdfValuePolicy` validates min/typ/max selection and compatible
    nonzero design-unit/simulation-precision identity, selects scalar or
    partial-triple exact values, multiplies decimal coefficients with governed
    arbitrary precision, and performs one exact half-up division at the final
    simulator tick boundary. The immutable result retains source/timescale
    values, negative-zero identity, rounding direction and versioned canonical
    identity. Five cataloged diagnostics reject invalid value/policy, absent
    selected slots, negative effective delay, decimal/power expansion and tick
    overflow at the source SDF coordinate. The warning-clean Debug target builds
    in four steps; value-policy, normalization, mapping, inventory, catalog and
    source gates pass 6/6 in 0.44 seconds. The refreshed graph contains 28,899
    nodes and 136,263 edges; the main selector has cognitive/cyclomatic
    complexity 8, no loops and no recursion. The ledger preserves Change 2 with
    sixteen active rows and SHA-256 identity
    `761b2e4ae15cd7868289cf91d02628ec78e22484b8bb6daad2bbb5439a6f7945`.
    Pinned formatting touched only the new implementation/test `.cpp` files;
    the semantic header received no formatting-only churn. No Release
    qualification, sanitizer or hosted-CI inspection ran. Preserve Changes 1-2
    and proceed to Change 3 without reset, commit or push.
22. Batch 169 Change 3 is complete in the same intentionally dirty worktree.
    `SdfAnnotationPlan` accepts only a complete validated mapping, exact value
    policy and Verilog/SystemVerilog target set. It verifies IR/cell/target
    identity and language, locates stable elaborated specify-path or timing-
    check objects, derives endpoint identities for other timing targets,
    selects all descendant exact values, freezes sorted endpoint IDs and
    before/after ticks, and publishes a versioned identity only after every
    entry succeeds. Four cataloged diagnostics own stale/type/language/count,
    missing target/arity, duplicate ownership and resource failures; propagated
    value-policy diagnostics retain their SDF coordinates. Tests prove planning
    leaves the elaborated path unchanged and that missing paths, duplicate
    claims, VHDL targets, invalid policy and identity ceilings publish no
    partial plan. The warning-clean Debug target builds in two steps; endpoint,
    mapping, value, plan, inventory, catalog and source gates pass 7/7 in 0.45
    seconds. The refreshed graph has 28,978 nodes and 136,843 edges; the plan
    builder has cognitive complexity 17, loop depth 2, no scan-in-loop site and
    no recursion. The ledger preserves Changes 2-3 with fifteen active rows and
    SHA-256 identity
    `56f8f94b7f8279d519864a2f6e3e28e8c7636d45313f939019d140f9b527c6a1`.
    Pinned formatting touched only new implementation/test `.cpp` files; the
    semantic header received no formatting-only churn. No Release
    qualification, sanitizer or hosted-CI inspection ran. Preserve Changes 1-3
    and proceed to Change 4 without reset, commit or push.
23. Batch 169 Change 4 is complete in the same intentionally dirty worktree.
    Target plans retain ancestor-derived absolute/increment mode plus condition
    and edge identities. `SdfPathTimingApplication` validates complete plan,
    stable path identity/instance/current delays, conditional/ifnone and edge
    compatibility and governed 1/2/3/6/12 arity. It produces copied specify
    paths with absolute replacement or checked same-shape increments while
    preserving path kind, edge, polarity, condition/data-source, pulse and
    source state; the elaborated design remains unchanged for rollback. Four
    cataloged diagnostics own stale targets/modes, condition/edge mismatch,
    arity and overflow/resource failures. Evidence covers unconditional and
    conditional edge-sensitive paths, both modes, source immutability, stale
    before-values, mismatches, overflow and identity ceilings. The warning-
    clean Debug target builds in five steps; endpoint/mapping/value/plan/path/
    inventory/catalog/source gates pass 8/8 in 0.45 seconds. The refreshed
    graph contains 29,037 nodes and 137,240 edges; the application builder has
    cognitive complexity 19, loop depth 1, no scan-in-loop site and no
    recursion. The ledger preserves Changes 2-4 with fourteen active rows and
    SHA-256 identity
    `f09db0bc7c9447f964fc98c1dd7f7fd92762a348caeb372e3e31df7ad42f78d3`.
    Pinned formatting touched only changed/new implementation/test `.cpp`
    files; semantic headers received no formatting-only churn. No Release
    qualification, sanitizer or hosted-CI inspection ran. Preserve Changes 1-4
    and proceed to Change 5 without reset, commit or push.
24. Batch 169 Change 5 is complete in the same intentionally dirty worktree.
    Target plans retain complete resolved endpoint descriptors in their
    versioned identity. `SdfInterconnectTimingApplication` validates exact
    Verilog/SystemVerilog HDL object kind, width/select/direction state,
    construct-specific role shape, unique endpoint ownership and governed
    transition arity before publishing immutable INTERCONNECT, PORT, MIPD/
    NETDELAY and DEVICE overlays. Each overlay preserves driver/load roles,
    instance/object boundaries, application mode, exact ticks and sorted
    process owners from static driver regions and switch terminals without
    mutating elaboration. Four diagnostics own incomplete/repeated targets,
    stale/ambiguous/unsupported endpoints, invalid modes/profiles and resource
    failure. Evidence covers all target families, continuous/gate/switch
    multi-driver retention, source immutability, native-object/stale-direction
    rejection, bad arity, duplicate endpoints and atomic resource rollback.
    The warning-clean Debug target builds in four steps; endpoint/mapping/value/
    plan/path/interconnect/inventory/catalog/source gates pass 9/9 in 0.44
    seconds. The refreshed graph contains 29,101 nodes and 137,587 edges; the
    application builder has cognitive complexity 18, loop depth 1, no scan-in-
    loop site and no recursion. The ledger preserves Changes 2-5 with thirteen
    active rows and SHA-256 identity
    `47667072d0e2285ede9191bf5b706aa0b6dca479645738410ae6100121ff7e27`.
    Pinned formatting touched only changed/new implementation/test `.cpp`
    files; semantic headers received no formatting-only churn. No Release
    qualification, sanitizer or hosted-CI inspection ran. Preserve Changes 1-5
    and proceed to Change 6 without reset, commit or push.
25. Batch 169 Change 6 is complete in the same intentionally dirty worktree.
    `SdfDelayModeApplication` expands governed 1/2/3/6/12 lists into the exact
    runtime-compatible twelve transition slots, including the established
    min/max X fallbacks. Ordered plans perform absolute replacement or checked
    incremental accumulation against a declared/prior-file profile and retain
    source, before, effective and ordering identities. Stale repeated-file
    state, missing increment baselines, invalid arity, overflow and resource
    limits reject atomically under four cataloged diagnostics. Tests cover all
    expansions, exact twelve-slot retention, repeated order, first-increment
    baselines and negative/resource boundaries. The warning-clean Debug target
    builds in four steps; the dependent slice passes 10/10 in 0.46 seconds.
    The refreshed graph contains 29,176 nodes and 137,892 edges; the coordinator
    has cognitive complexity 8, loop depth 2, no scan-in-loop site and no
    recursion, and its per-annotation helper is loop-free. The ledger preserves
    Changes 2-6 with twelve active rows and SHA-256 identity
    `568c676e4472604a56eeab7c9c81fc24bfcd83b5c8e632d19905f202a6ea9a74`.
    No Release qualification, sanitizer, hosted-CI inspection or header
    formatting ran. Preserve Changes 1-6 and proceed to Change 7 without reset,
    commit or push.
26. Batch 169 Change 7 is complete in the same intentionally dirty worktree.
    `SdfPrimaryTimingCheckApplication` validates stable check identity, exact
    setup/hold/setuphold/recovery/removal/recrem kind, reference/data endpoint
    roles and current runtime-legal signed limits before publishing copied overlays.
    Simple checks receive one limit; combined checks retain two distinct limits
    and optional delayed reference/data terminals, conditions, notifier,
    runtime state and source coordinates. Four diagnostics reject incomplete,
    missing, repeated, kind/event mismatched, stale/partial, overflow and
    resource state atomically. Tests cover all six kinds and every owned
    negative boundary while proving source immutability. The ledger preserves
    Changes 2-7 with eleven active rows and SHA-256 identity
    `ef512c30adab581217257efd2ff26e93e0d04faa8650193036e6635e8924a64e`.
    The warning-clean Debug target builds in four steps; the dependent slice
    passes 11/11 in 0.47 seconds. The refreshed graph contains 29,233 nodes and
    138,219 edges; the application builder has cognitive complexity 25, loop
    depth 2, no scan-in-loop site and no recursion.
    No Release qualification, sanitizer, hosted-CI inspection or header
    formatting ran. Preserve Changes 1-7 and proceed to Change 8 without reset,
    commit or push.
27. Batch 169 Change 8 is complete in the same intentionally dirty worktree.
    `SdfSecondaryTimingCheckApplication` applies copied skew/timeskew/fullskew/
    width/period/nochange overlays after validating stable kind, exact event
    roles, current limits and one/two-limit shapes. Threshold, notifier,
    conditions, event-based/remain-active state and source coordinates remain
    intact; reversed nochange windows reject. Tests cover all six runtime kinds,
    fullskew pairs, width/runtime state, event/order negatives and atomic
    resources. The ledger preserves Changes 2-8 with ten active rows and digest
    `dfa52e7cfde7ee2fb31512fb2a9fc762663987047777fbe7c630158d7791eef2`.
    The warning-clean Debug target builds in four steps; the dependent slice
    passes 12/12 in 0.46 seconds. The refreshed graph contains 29,290 nodes and
    138,522 edges; the application builder has cognitive complexity 23, loop
    depth 2, no scan-in-loop site and no recursion.
    No Release qualification, sanitizer, hosted-CI inspection or header
    formatting ran. Preserve Changes 1-8 and proceed to Change 9 without reset,
    commit or push.
28. Batch 169 Change 9 is complete in the same intentionally dirty worktree.
    Signed timing-check selection and target-plan schema 3 retain exact signed
    before/after ticks separately from unsigned delays, including negative zero,
    the exact signed minimum, setuphold/recrem positive-sum windows and signed
    nochange ranges. Primary and secondary schema-2 overlays now consume this
    state. `SdfConditionTimingApplication` binds SDF conditions and edges to the
    existing elaborated recursion-free programs, notifier and dense stable
    order, and exposes matched, elaborated-only and unconditional applicability
    without reparsing HDL or widening a mismatch. Runtime evidence preserves X/Z
    suppression, true compound-condition notifier toggling and repeatable same-
    tick outcomes; partial conditions, stale edges, illegal signed windows and
    resource ceilings reject atomically under four diagnostics. The ledger
    preserves Changes 2-9 with nine active rows and digest
    `9a2c7ee0afd8355971e994c84eb0afa5fc590d56d9cbe3e389d2cf9e8649e835`.
    The warning-clean final eight-worker Debug rebuild takes eight steps; the
    dependent application/inventory/catalog/source slice passes 13/13 in 0.45
    seconds. The refreshed graph contains 29,385 nodes and 138,993 edges; the
    application builder has cognitive complexity 22, loop depth 1, no scan-in-
    loop site and no recursion. Pinned formatting touched changed `.cpp` files
    only; semantic header additions received no formatting churn. No Release,
    sanitizer, hosted-CI inspection, reset, commit or push ran. Preserve Changes
    1-9 and proceed to Change 10.
29. Batch 169 Change 10 is complete in the same intentionally dirty worktree.
    Target-plan schema 4 separates exact percentage and embedded RETAIN values
    from ordinary delays. `SdfPulseTimingApplication` publishes immutable
    per-path absolute or exactly scaled percentage reject/error tables and
    RETAIN tables, expands endpoint-free SDF 3.0/4.0 percentage annotations in
    stable path-ID order, and confines endpoint-bearing `GLOBALPATHPULSE` to its
    SDF 2.1 profile and linked path. It preserves source polarity, pulse style,
    show-cancelled policy, transition identity and elaborated timing state.
    Runtime module paths select governed 1/2/3/6/12-entry pulse/RETAIN values
    through the same 0/1/X/Z transition identity as the path delay, retain the
    prior value until the exact onset boundary, suppress zero-width X windows
    and recover without stale cancellation. Real pipeline and runtime evidence
    covers targeted PATHPULSE plus embedded RETAIN, global percentages, legacy
    SDF 2.1, reject/error equality boundaries, sub-reject and zero-width
    cancellation, onevent/ondetect, 12-entry Z transitions and atomic stale,
    conflict, forged-value, threshold-order and resource rejection under four
    diagnostics. The warning-clean eight-worker Debug builds and complete
    dependent application, specify/runtime, inventory/catalog/source slice pass
    16/16 in 1.09 seconds. The refreshed graph contains 31,533 nodes and 144,557
    edges; the application builder has cognitive complexity 8, loop depth 1,
    no scan-in-loop site and no recursion, while exact percentage-to-tick
    conversion is loop-free with cognitive complexity 9. The ledger preserves
    Changes 2-10 with eight active rows and digest
    `c5c7f5e01da4c000cc36eef30e49a76c11af3a0e39b192b4a52cff61761d85a6`.
    Pinned formatting touched changed `.cpp` files only; semantic header edits
    received no formatting-only churn. No Release, sanitizer, hosted-CI
    inspection, reset, commit or push ran. Preserve Changes 1-10 and proceed to
    Change 11.
30. Batch 169 Change 11 is complete in the same intentionally dirty worktree.
    `SdfPrecedenceApplication` publishes immutable per-value precedence for
    exact source/effective specify, primitive/net endpoint, timing-check,
    PATHPULSE/PATHPULSEPERCENT and RETAIN values. Every value records its stable
    target/index, selected source, absolute/increment mode, command min/typ/max
    selection, enabled state, annotation provenance and semantic identity.
    Map-indexed checks reject stale/missing source paths and timing checks;
    incompatible command selection and enabled pulse rejection on disabled
    paths reject as contradictory policies. No-annotation zero-delay paths and
    timing checks remain byte-identical where observable, repeated applications
    are identity-stable and elaboration is never mutated. Positive/negative
    evidence covers absolute/increment, endpoint, check, pulse/RETAIN, disabled
    domains, stale targets and atomic resource rollback under four diagnostics.
    The warning-clean eight-worker Debug target builds; the final dependent
    application, specify/runtime, inventory/catalog/source slice passes 17/17
    in 1.20 seconds and the final static slice passes 3/3 in 0.44 seconds. The
    refreshed graph contains 31,636 nodes and 145,090 edges; the loop-free
    coordinator has cognitive/cyclomatic complexity 7 and no scan-in-loop or
    recursion, while the map-indexed delay validator has cognitive 17, loop
    depth 1, no scan-in-loop or recursion. The ledger preserves Changes 2-11
    with seven active rows and digest
    `02d505e919d5c2d9f82783761ade680fd20e7825c57702bccb60a19e04b0fd85`.
    Pinned formatting touched changed/new `.cpp` files only; the required
    semantic public header received no formatting-only churn. No Release build
    or test, sanitizer, hosted-CI inspection, reset, commit or push ran.
    Preserve Changes 1-11 and proceed to Change 12.
31. Batch 169 Change 12 is complete in the same intentionally dirty worktree.
    `SdfSchedulingApplication` freezes Change 11 precedence into a copied
    runtime design before interpreter or LLVM execution: selected absolute/
    increment paths carry exact twelve-transition tables, timing checks carry
    signed selected limits and PATHPULSE reject/error plus RETAIN use the
    existing scheduler-owned pulse state. Source-selected, disabled and no-
    annotation profiles retain their original compact runtime representation.
    Compiled writes therefore use the same interpreter-owned inertial/path
    scheduler without per-event hierarchy/annotation lookup. Runtime evidence
    covers cancellation plus RETAIN onset/recovery; a real two-instance
    SystemVerilog specify project proves identical interpreter/LLVM values and
    stable same-time multi-producer order. Source immutability and atomic
    missing/stale/index/resource negatives pass under four diagnostics. The
    warning-clean final eight-worker Debug build takes four steps; the complete
    dependent application, interpreter/LLVM, specify/runtime, inventory/catalog/
    source slice passes 18/18 in 1.29 seconds and the final static slice passes
    3/3 in 0.44 seconds. The refreshed graph contains
    31,703 nodes and 145,633 edges; the coordinator has cognitive 11, loop depth
    1, no scan-in-loop or recursion, and target/path builders have cognitive 12,
    maximum loop depth 2 and no scan-in-loop or recursion. The ledger preserves
    Changes 2-12 with six active rows and digest
    `0c5e154e75538dde38760533018902769edf9ec4349c9abbd43ee67fbc4f850a`.
    Pinned formatting touched changed/new `.cpp` files only; the required
    scheduling header received no formatting-only churn. No Release build/test,
    sanitizer, hosted-CI inspection, reset, commit or push ran. Preserve Changes
    1-12 and proceed to Change 13.
32. Batch 169 Change 13 is complete in the same intentionally dirty worktree.
    `SdfDriveTimingApplication` publishes bounded, immutable path-driver,
    propagated continuous-driver, unidirectional MOS/CMOS and bidirectional
    tran ownership from the Change 12 scheduled design. A one-time bounded
    static-fanout closure carries exact packed/whole driver regions, strength
    and resolution through output projections before switch binding; runtime
    events perform no hierarchy, ownership or annotation lookup. Direct scalar
    and vector evidence records strong annotated updates at ticks 3 and 7 under
    a force, delayed Z driver removal, weak-driver resolution and tran-peer
    propagation; release exposes the current underlying resolved value. A real
    strengthened SystemVerilog specify project with a competing weak continuous
    assignment, packed tran paths and procedural force/release produces
    identical interpreter/LLVM traces, including `11` at tick 6 and `00` at
    tick 13, with compiled processes present. Missing scheduling/ownership,
    invalid or duplicate switch bindings and resource ceilings reject atomically
    under four cataloged diagnostics. The final warning-clean eight-worker
    Debug target and dependent application/specify/resolution/inventory slice
    pass 19/19 in 2.01 seconds; the final static slice passes 3/3 in 0.43
    seconds. The refreshed graph contains 31,782 nodes and 146,087 edges; the
    coordinator has cognitive 13, maximum loop depth 2 and no scan-in-loop or
    recursion, while bounded fanout coordination has cognitive 9, maximum loop
    depth 2 and no scan-in-loop or recursion. The ledger preserves Changes 2-13
    with five active rows and digest
    `0b6d5db771c0241fd7089aeb8abfb703bd39a9df088ed66769f0bfe599b1b0ee`.
    Pinned formatting touched changed/new `.cpp` files only; the required
    semantic drive-timing header received no formatting-only churn. No Release
    build/test, sanitizer, hosted-CI inspection, reset, commit or push ran.
    Preserve Changes 1-13 and proceed to Change 14.
33. Batch 169 Change 14 is complete in the same intentionally dirty worktree.
    `SdfReannotationApplication` composes multiple SDF timing publications by
    explicit file/cell precedence across bounded iterative `*`/`?` scopes and
    multiple elaboration roots. It validates every layer against the baseline
    path/check topology before constructing the effective design; equal-
    precedence duplicates and conflicts reject separately, higher precedence
    wins independent of input order, and no failed transaction publishes.
    Runtime reannotation is legal before start or only in the scheduler's
    explicit between-phase safe point. Complete replacement paths/checks are
    validated before no-throw swaps. Pending path writes keep their scheduled
    delay, timing-check timestamps/windows/deadlines retain their state, and
    later events observe the new immutable generation. Direct two-root evidence
    preserves a pre-commit path at tick 5, moves three concurrent post-commit
    paths together to tick 3, retains a pre-commit reference timestamp for a
    post-commit hold violation and gives repeated same-safe-point observers the
    same generation/identity. Missing baseline/scope/safe-point state, exact
    duplicates, conflicts/topology changes and resource ceilings reject
    atomically under four diagnostics. The final eight-worker Debug target is
    warning-clean and current with no work; the complete dependent application,
    specify, resolution and inventory slice passes 20/20 in 2.08 seconds. The
    diagnostic/source/inventory slice passes 3/3. The refreshed graph contains
    31,875 nodes and 146,917 edges; the one-time coordinator has cognitive 33,
    maximum loop depth 2 and three bounded setup scan sites, safe-point commit
    has cognitive 5 with no scan site, and runtime validation has cognitive 12
    and loop depth 1. No annotation lookup enters the event hot path. The ledger
    preserves Changes 2-14 with four active rows and digest
    `a60da63cd4132f7949e2da9050c24e6d7b3d7d46ed781fbf42387fcd94f60c7a`.
    Pinned formatting touched changed `.cpp` files only; required public-header
    edits were semantic and no header formatting churn occurred. Per the Batch
    169 policy, no Release build/test ran; no sanitizer, hosted-CI inspection,
    reset, commit or push ran. Preserve Changes 1-14 and proceed to Change 15.
34. Batch 169 Change 15 is complete in the same intentionally dirty worktree.
    `SdfControlApplication` publishes immutable, versioned annotation requests,
    exact counts, stable source/object/application identities and bounded
    detailed reports. Project CLI accepts ordered normalized `--sdf` inputs,
    root/cell/report selectors and min/typ/max mode only for elaborate/simulate
    capable commands. Tcl supplies atomic configure plus callback-safe summary
    and report commands. The append-only C ABI supplies versioned configure,
    summary and report-entry records; failed configuration retains the prior
    application. Compile-phase input, duplicate scope, phase/generation/source
    provenance mismatch and resource ceilings reject under four cataloged
    diagnostics. The focused C++/CLI/C-API executable and Tcl batch coverage
    pass, including failed-mutation rollback; the complete dependent SDF,
    specify, resolution and inventory slice passes 21/21 in 1.31 seconds and
    the public API/C-header/Tcl/static slice passes 7/7 in 1.50 seconds. The
    final eight-worker Debug target is warning-clean and current with no work.
    The refreshed graph contains 32,028 nodes and 147,504 edges; the immutable
    coordinator has cognitive 22, maximum loop depth 1 and no scan-in-loop or
    recursion, while the Tcl and C adapters have cognitive 19 and 9 with loop
    depth 1. The ledger preserves Changes 2-15 with three active rows and digest
    `f0ece19204046b96bf9798c8e035983da9af37bf9c76df40f3d5872fdf8f87c2`.
    Formatting touched changed `.cpp` files only; semantic public-header changes
    received no formatting-only churn. Per Batch 169 policy, no Release
    build/test ran; no sanitizer, hosted-CI inspection, reset, commit or push
    ran. Preserve Changes 1-15 and proceed to Change 16.
35. Batch 169 Change 16 is complete in the same intentionally dirty worktree.
    Schema-1 `FSDFEFF` archives canonically preserve effective SDF original and
    selected values, target/source/root/cell identities, precedence, policy and
    provenance for object, design, mapped-library, native-cache and checkpoint
    consumers. Envelopes bind artifact kind and logical producer and carry
    exact payload size/checksum. Current-schema decode requires the expected
    kind, producer and policy; producer-relative sources survive relocation and
    cold/warm cache and checkpoint replay reproduce one archive identity.
    Stale/corrupt/cross-kind/cross-producer/cross-policy records, duplicate
    targets, absolute provenance and resource ceilings reject atomically under
    four cataloged diagnostics with no superseded-schema reader. The final
    eight-worker Debug target is warning-clean and current with no work; the
    complete dependent SDF/artifact/cache/static/specify/resolution slice passes
    30/30 in 1.26 seconds. The refreshed graph contains 32,118 nodes and 147,933
    edges; encode/decode/validation have cognitive complexity 4/11/12, maximum
    loop depth 1 and no scan-in-loop or recursion. The ledger preserves Changes
    2-16 with two active rows and digest
    `f4943c4e2b98e4be05fd0c0371d074dd4bd70e59213476a494d90ae994de7b2d`.
    Formatting touched changed `.cpp` files only; the semantic archive header
    received no formatting-only churn. Per Batch 169 policy, no Release
    build/test ran; no sanitizer, hosted-CI inspection, reset, commit or push
    ran. Preserve Changes 1-16 and proceed to Change 17.
36. Batch 169 Change 17 is complete in the same intentionally dirty worktree.
    `SdfObservabilityApplication` canonically enumerates effective annotated
    targets with stable debugger IDs, VPI handles, VCD names, original/effective
    values and source spans. One preallocated recorder fans fixed-size violation
    records to selected debugger, callback, internal-trace, VPI and VCD views,
    retaining scheduler region and a common monotonic sequence without changing
    scheduling. Disabled observation reserves no event storage and returns
    before lookup/allocation. Unknown targets, invalid regions/value arity and
    full buffers reject without partial fanout; incomplete/duplicate targets and
    resource ceilings reject setup under four cataloged diagnostics. The final
    warning-clean eight-worker Debug target is current with no work and the
    complete SDF/debugger/VPI/VCD/API/static/specify/resolution slice passes
    31/31 in 1.61 seconds. The refreshed graph contains 32,213 nodes and 148,359
    edges; catalog build and fixed-surface recording have cognitive complexity
    10 and 9 with maximum loop depth 1 and no recursion. The ledger preserves
    Changes 2-17 with one active row and digest
    `102b109a21aa0f291da666f6873e98751d8e8b0016db66040575014d7205edae`.
    Formatting touched changed `.cpp` files only; the semantic observability
    header received no formatting-only churn. Per Batch 169 policy, no Release
    build/test ran; no sanitizer, hosted-CI inspection, reset, commit or push
    ran. Preserve Changes 1-17 and proceed to Change 18.
37. Batch 169 Change 18 is complete in the same intentionally dirty worktree.
    The clean-room application corpus owns standard-cell, primitive,
    interconnect, pulse and timing-check rows with exact before/after values,
    source spans, two observed violation classes and all effective artifact/
    cache/checkpoint consumers. The seven-stage serial closure requires real
    time advancement, timing, violation, interpreter/LLVM, optimized/debug,
    project/non-project, artifact, cold/warm/relocated cache, replay, public
    observation, Linux/Windows contract, negative-family and clean-exit tokens;
    it rejects a passing child without them. The closure passes in 33.25 seconds
    and retains one console transcript, seven verbose stage logs totaling 30,437
    bytes and a seven-row PASS result TSV. The refreshed graph contains 32,230
    nodes and 148,489 edges. The ledger preserves all seventeen Changes 2-18
    rows with digest
    `47e7f5b95aae9f0e3df5cb4fcb4939255e1803f9c920081a47198e21b75f2754`.
    Formatting touched the new `.cpp` witness only and no header formatting ran.
    Per Batch 169 policy, no Release build/test ran; no sanitizer, hosted-CI
    inspection, reset, commit or push ran. Preserve Changes 1-18 and proceed to
    Change 19.
38. Batch 169 Change 19 is complete in the same intentionally dirty worktree.
    Public SDF, README, language, architecture, native-API, diagnostic, example
    and feature-matrix documentation now owns the implemented Verilog/
    SystemVerilog application boundary and explicitly leaves VHDL/VITAL timing
    to Batch 170. `fsim.sdf-application-release-audit` composes the ledger,
    catalog and source gates; verifies five review IDs and the public boundary;
    and passes with seventeen preserved rows, zero active rows, 2,389 production
    diagnostics, 974 bounded C/C++ sources, 1,139 SPDX-owned artifacts and
    digest
    `47e7f5b95aae9f0e3df5cb4fcb4939255e1803f9c920081a47198e21b75f2754`.
    The audit retains the Change 18 closure's 33.25-second runtime, 30,437 log
    bytes, positive time advancement, clean exit and 32,230-node/148,489-edge
    graph evidence. Change 19 changed documentation, examples and CMake
    registration only; no header formatting or compilation ran. Per policy,
    no Release build/test, sanitizer, hosted-CI inspection, reset, commit or
    push ran. Preserve Changes 1-19 and proceed to final Change 20 checks.
39. Batch 169 Change 20 is complete. Fresh clean-first exact-LLVM 22.1.8 Debug
    and Release builds pass all 874 steps warning-free with eight workers in
    12:18.95 and 10:03.81, at 5,141,632 and 2,255,432 KiB peak RSS and zero
    swaps. Final complete non-sanitized Debug and Release regressions pass
    182/182 in 8:45.20 and 7:37.31, with CTest totals of 525.19 and 457.30
    seconds, peak RSS of 3,780,536 and 3,788,200 KiB and zero swaps. The final
    SDF application closures pass in 41.99 and 37.91 seconds. All source,
    catalog, inventory, installed-public, relocation, differential, resource,
    platform and release gates are included. Eight primary build/test logs and
    metric records retain 399,038 bytes, report exit zero and contain no
    compiler warning/error marker. The refreshed graph contains 32,232 nodes
    and 148,494 edges. No header formatting, sanitizer or hosted-CI inspection
    ran. The sole accumulated implementation checkpoint is
    `4c00c765105e32815816f3c25a490423c1603074`; the following documentation-only restart
    checkpoint records its exact hash and begins Batch 170.

## Batch 170 planned restart checkpoint - prepared 2026-08-13

1. Batch 169 Change 20 is locally complete with the retained measurements in
   item 39. Start Batch 170 only after the sole Batch 169 implementation commit
   `4c00c765105e32815816f3c25a490423c1603074` and this documentation-only restart
   checkpoint are pushed and branch `codex/v2` is clean and synchronized.
2. Start in `/home/colin/projects/fsim`, read this section and the authoritative
   Batch 170 allocation in `implementation_plan_v2.md`, verify branch
   `codex/v2` is clean and synchronized, and verify the Batch 169 SDF
   application ledger still has seventeen preserved rows with digest
   `47e7f5b95aae9f0e3df5cb4fcb4939255e1803f9c920081a47198e21b75f2754`.
3. Preserve Batch 168 parsing/normalization/resolution/persistence and Batch
   169 Verilog/SystemVerilog timing application. Batch 170 extends those exact
   records to VHDL/VITAL primitives, paths, checks and mixed-language timing;
   it does not replace or weaken either earlier boundary.
4. Changes 1-4 map cells, ports, generics, paths and checks to VHDL/VITAL
   primitives, delay records, wire/path functions, state tables, memory models
   and governed wrappers. Changes 5-8 own precedence, timing generics, exact
   min/typ/max, transport/inertial/reject/pulse, negative checks and
   reannotation.
5. Changes 9-12 own cross-language interconnect/timing through VHDL, Verilog,
   SystemVerilog and SystemC proxy boundaries with explicit conversion,
   resolver, multiple-root and path identity. Changes 13-16 own VHPI/VPI,
   debugger/callback/trace, artifacts, mapped libraries, relocation, caches and
   non-project phases.
6. Changes 17-19 close owned standard-cell/memory corpora, mismatch/ambiguity/
   resource negatives, engine/platform evidence, public documentation,
   matrices, inventories and the next handoff. Change 20 alone runs sanitizer,
   full Debug/Release and all release gates, commits/pushes once, then inspects
   and repairs every non-documentation hosted-CI job.
7. Batch 170 is a sanitizer and hosted-CI monitoring boundary. Use at least
   eight workers locally, retain logs and resource measurements, and do not
   confuse a zero exit status with valid simulation evidence: witnesses must
   advance time, print their exact PASS token and terminate cleanly.
8. For Changes 1-19, Release build/testing is not required. Avoid formatting-
   only header changes that induce long rebuilds; make semantic header edits
   only when required and format changed implementation sources without
   churning unchanged headers.

## Batch 168 planned restart checkpoint - 2026-08-13

1. Start in `/home/colin/projects/fsim`, read this section and the authoritative
   expanded Batch 168 allocation in `implementation_plan_v2.md`, and verify
   branch `codex/v2` is clean and synchronized at pushed Batch 167 closeout
   `7f9fce1f6a0dc658ead37291f9672ba1b10fe5c1` plus this documentation-only
   Batch 168 planning checkpoint.
2. This checkpoint implements the required pre-batch flow. Batch 168 is
   expanded into exactly twenty changes without broadening its locked SDF 4.0
   parser, normalization, resolution and artifact scope. No Batch 168
   implementation file has changed; clear context after pushing this plan and
   resume only from this section and the authoritative allocation.
3. Preserve Batch 167's explicit Verilog-1995/2001/2001-noconfig and
   SystemVerilog-2005/2009/2012 modes beside the Verilog-2005 and
   SystemVerilog-2017 defaults. Preserve typed revision/profile identity through
   preprocessing, semantics, execution, APIs, libraries, artifacts, caches,
   relocation, checkpoint/replay and non-project phases.
4. Preserve Batch 167's two 17-row ledgers with zero active obligations, six
   revision and seven compatibility corpus rows, and the retained 16-witness
   closure matrix. Their final inventory identities are
   `21a05e4732aa8da8b546f2c7048ee2b5d51cdca4572114fb5c9946d4bfad8d3e`
   and
   `5e7a74d4f9c0af29e28c4d0b9b35b96a5c9df6482b53759633f80904812c5f6f`.
5. Batch 167's fresh clean-first exact-LLVM 22.1.8 Debug and Release
   eight-worker builds complete 781/781 steps warning-free in 11:55.31 and
   9:36.37 at 5,030,336 and 2,254,716 KiB peak RSS with zero swaps.
6. Complete non-sanitized Debug and Release regressions pass 146/146 in 9:11.10
   and 7:59.35 wall time, with CTest totals of 551.08 and 479.34 seconds, peak
   RSS of 3,781,532 and 3,750,768 KiB, and zero swaps. Final post-documentation
   static/release slices pass 42/42 in 22.84 and 21.63 seconds.
7. Batch 167 closeout repairs legal Verilog `timescale` lexical validation,
   semantic ownership of system-function arity, Verilog library export/import
   metadata identity and the synchronized v1 feature digest. Pinned formatter
   work was confined to changed `.cpp` files; no public header formatting churn
   was introduced. Batch 167 ran neither sanitizer nor hosted-CI inspection.
8. Batch 168 implements a clean-room SDF 4.0 parser, explicit SDF 2.1/3.0
   revision adapters, one immutable normalized SDF IR, deterministic resolution
   against elaborated multi-root mixed-language hierarchy and versioned portable
   persistence. It does not apply SDF to simulator timing behavior.
9. Changes 1-4 freeze the allocation and close bounded lexical, complete SDF
   4.0 header and complete cell/delay/timing-check/timing-environment grammar
   with precise source spans and resource diagnostics.
10. Changes 5-8 implement independent SDF 2.1 and 3.0 adapters, exact decimal/
    triple/timescale/name normalization and one immutable ordered IR shared by
    all accepted revisions without losing revision-specific distinctions.
11. Changes 9-12 define annotation scope and resolve celltype, exact/wildcard
    instance, port/interconnect/device/path and timing-check references across
    multi-root Verilog/SystemVerilog/VHDL/SystemC hierarchy. Resolution publishes
    nothing until all missing, ambiguous, duplicate and conflict checks pass.
12. Changes 13-16 version and preserve syntax, normalized IR, resolved mappings,
    provenance, timescale, policies, digests and semantic identities through
    design/library artifacts, relocation, non-project phases and cold/warm
    caches with strict stale/corrupt/future-format rejection.
13. Changes 17-19 publish standard-specific positive corpora, exhaustive
    parser/schema/resolution/resource negatives, retained-log cross-engine and
    platform differentials, then synchronize public docs, diagnostics, matrices,
    inventories, counts, digests and this handoff.
14. Batches 169 and 170 retain all timing-application work. Batch 169 applies
    SDF to Verilog/SystemVerilog specify/primitive behavior; Batch 170 applies
    it to VHDL/VITAL and mixed-language timing. Do not partially implement those
    behaviors in Batch 168.
15. Accumulate Changes 1-19 in one recoverable worktree with focused dependent
    Debug validation. Release build/testing is not required before the final
    Change 20 batch checks. Avoid header-only formatting changes that induce
    long rebuilds; make semantic header edits only when the owning change needs
    them and format implementation hunks without churning unchanged headers.
16. Change 20 alone runs fresh clean-first exact-LLVM Debug and Release builds
    with at least eight workers and 120-minute command timeouts, full non-
    sanitized regressions and every release gate. Retain timing/RSS/swap/log
    evidence and create the sole Batch 168 implementation commit/push only
    after all local gates pass. Batch 168 is not a hosted-CI monitoring boundary.
17. Do not reset, commit or push accumulated Batch 168 implementation before
    Change 20. After that implementation commit, save and push the exact Batch
    169 restart plan and clear context before Batch 169 implementation.
18. Batch 168 Change 1 is complete in the intentionally dirty worktree. The
    authoritative 17-row ledger assigns Changes 2-18 one-to-one across lexical,
    header, SDF 4.0 construct, SDF 2.1/3.0 adapter, exact-value normalization,
    immutable IR, hierarchy resolution, mapping validation, schema/artifact,
    relocation/cache, corpus and closure domains. Every active row binds all
    three governed revisions plus exact implementation, positive, negative,
    revision, diagnostic-coordinate, portable-round-trip, diagnostic-catalog
    and resource owners. The registered gate freezes unique IDs/closures,
    ordered domains, plan allocation, owner path classes and SHA-256 identity
    `55025a506669f4596b9962e06e6f75dc6546bc55ec6e50523b44382ed55727af`.
    The exact-LLVM Debug tree regenerates without a source rebuild; inventory,
    diagnostic-catalog and source-budget gates pass 3/3 in 0.25 seconds. No
    Release qualification, sanitizer, hosted-CI inspection or header formatting
    ran. Preserve Change 1 and proceed to Change 2 without reset, commit or
    push.
19. Batch 168 Change 2 is complete in the same intentionally dirty worktree. A
    standalone clean-room SDF lexer retains line/block comments, quoted
    strings, plain and escaped divider-qualified names, keywords, delimiters,
    signed decimal/realtime spellings and triple separators with exact physical
    byte and logical line/column spans across LF, CRLF and CR. Eleven cataloged
    diagnostics cover invalid bytes, unterminated forms, dangling escapes,
    malformed exponents, unmatched nesting and the governed 64 MiB source,
    1,000,000-token, 4,096-depth, 1 MiB token and 4,096-byte numeric ceilings.
    The dedicated warning-clean Debug target and existing frontend pass with
    inventory, diagnostic and source gates 5/5 in 0.25 seconds. Graph audit
    reports maximum cognitive complexity 25, loop depth 1, no scan-in-loop site
    and no recursion. The ledger preserves Change 2 with sixteen active rows
    and SHA-256 identity
    `27f6590b53d323f6f961dd87ebb0bd5cca87be317caa68b2ff1f47f34c79045a`.
    Pinned formatting touched only the new lexer/test `.cpp` files; the new
    semantic public header received no formatting-only churn. No Release
    qualification, sanitizer or hosted-CI inspection ran. Preserve Changes 1-2
    and proceed to Change 3 without reset, commit or push.
20. Batch 168 Change 3 is complete in the same intentionally dirty worktree.
    The typed parser validates the DELAYFILE root and complete ordered
    SDFVERSION, DESIGN, DATE, VENDOR, PROGRAM, VERSION, DIVIDER, VOLTAGE,
    PROCESS, TEMPERATURE and TIMESCALE surface. It retains original keyword/
    value spellings, decoded canonical strings, exact decimal triples,
    normalized divider/timescale identity, comments/tokens, physical spans and
    ordered raw CELL forms for Change 4. Known disabled and future revisions,
    malformed values, duplicates, ordering, missing headers, late headers,
    unknown forms and trailing tokens receive ten cataloged parser diagnostics.
    The warning-clean Debug parser, lexer and existing frontend targets pass
    with inventory, catalog and source gates 6/6 in 0.25 seconds. Graph audit
    reports maximum cognitive complexity 23, loop depth 1, no scan-in-loop site
    and no recursion. The ledger preserves Changes 2-3 with fifteen active rows
    and SHA-256 identity
    `ee048625fa4588fc6efebb89e797062a77c69857b4d0449c0a9dda3896257f4f`.
    Pinned formatting touched only the parser/test `.cpp` files; the semantic
    public-header addition received no formatting-only churn. No Release
    qualification, sanitizer or hosted-CI inspection ran. Preserve Changes 1-3
    and proceed to Change 4 without reset, commit or push.
21. Batch 168 Change 4 is complete in the same intentionally dirty worktree.
    The non-recursive SDF 4.0 construct parser retains each CELL's exact
    celltype, exact/wildcard/empty instance selector, ordered syntax nodes,
    original token interval and physical span. It covers ABSOLUTE/INCREMENT
    delay forms, conditional/else IOPATH and RETAIN, edge, port, interconnect,
    device, MIPD and pulse forms; all timing checks and their optional/grouped
    conditions; timing-environment path/period/skew, sum/diff, arrival,
    departure, slack, waveform and exception forms; and arbitrary label
    entries. Ten cataloged construct diagnostics isolate root/header,
    placement, arity, value, edge, condition, order and unknown-form failures.
    The warning-clean Debug construct, parser, lexer and existing frontend
    targets pass with inventory, catalog and source gates 7/7 in 0.29 seconds.
    Graph audit reports maximum cognitive complexity 25, bounded local loop
    depth 2 for the iterative worklist, and no recursion. The ledger preserves
    Changes 2-4 with fourteen active rows and SHA-256 identity
    `6eaa4d43839cae37a0b454b8f73a24a3e3f715b192f0c83c10f9e4fd38c7e7d5`.
    Pinned formatting touched only the construct parser/test `.cpp` files; the
    public-header changes were semantic only and formatting-only header churn
    was avoided. No Release qualification, sanitizer or hosted-CI inspection
    ran. Preserve Changes 1-4 and proceed to Change 5 without reset, commit or
    push.
22. Batch 168 Change 5 is complete in the same intentionally dirty worktree.
    A dedicated OVI SDF 2.1 adapter retains exact version provenance and a
    typed adapter identity; maps decimal legacy timescales, repeated INSTANCE
    segments, wildcard restriction, CORRELATION, GLOBALPATHPULSE, NETDELAY,
    legacy edge spellings, governed delay lists and timing-check/constraint
    forms into the common syntax vocabulary; and rejects later-only profiles
    without enabling SDF 3.0. Six cataloged diagnostics own profile, repeated
    selector/correlation, mixed scalar/triple, delay-list, constraint and
    timescale failures. The warning-clean Debug adapter and all prior SDF/
    frontend targets pass with inventory, catalog and source gates 8/8 in 0.28
    seconds. Graph audit reports maximum cognitive complexity 19, bounded local
    loop depth 2 and no recursion. The ledger preserves Changes 2-5 with
    thirteen active rows and SHA-256 identity
    `f0ace636e3c9339a26335bd1d40cc3eb564254ecd5bac7467a453941cfd82b0f`.
    Pinned formatting touched only changed `.cpp` files; public-header edits
    were semantic only and formatting-only header churn was avoided. No Release
    qualification, sanitizer or hosted-CI inspection ran. Preserve Changes 1-5
    and proceed to Change 6 without reset, commit or push.
23. Batch 168 Change 6 is complete in the same intentionally dirty worktree.
    The independent OVI SDF 3.0 adapter retains exact revision/adapter
    provenance and accepts its modern single INSTANCE, decimal legacy
    timescale, pulse/delay-list, named condition, CONDELSE, RETAIN, combined
    timing-check and TIMINGENV surfaces. It rejects removed 2.1 CORRELATION,
    GLOBALPATHPULSE and NETDELAY plus 4.0-only LABEL and MIPD without
    contaminating the adjacent adapters. Four cataloged diagnostics own
    profile, repeated-instance, timescale and delay-list/value-style failures.
    The warning-clean Debug adapter and all prior SDF/frontend targets pass
    with inventory, catalog and source gates 9/9 in 0.25 seconds. Graph audit
    reports maximum cognitive complexity 13, bounded local loop depth 2 and no
    recursion. The ledger preserves Changes 2-6 with twelve active rows and
    SHA-256 identity
    `3688226ce242c2b7e04efc7f021e4c1616fac1aabc2a42e15556680b98742908`.
    Pinned formatting touched only changed `.cpp` files; public-header edits
    were semantic only and formatting-only header churn was avoided. No
    Release qualification, sanitizer or hosted-CI inspection ran. Preserve
    Changes 1-6 and proceed to Change 7 without reset, commit or push.
24. Batch 168 Change 7 is complete in the same intentionally dirty worktree.
    Exact normalization represents governed decimals as unbounded coefficient
    strings plus checked base-ten exponents, preserves empty/scalar and every
    present or missing min/typ/max slot, and scales time values to exact
    rational femtoseconds without floating point. Typed timescale units,
    case-stable edges, exact condition atoms, divider-independent hierarchy
    segments and escaped characters receive canonical identities. The bounded
    exact-integer conversion API reports loss and expansion overflow rather
    than rounding; parsing selects neither a triple component nor simulator
    precision. Four cataloged normalization diagnostics cover exponent, shape,
    scaling and hierarchy failures. Evidence includes 4,000-digit values,
    negative zero, partial triples, fractional femtoseconds and equivalent
    2.1/3.0/4.0 inputs. The warning-clean Debug targets, inventory, catalog and
    source gates pass 10/10 in 0.55 seconds. Graph audit reports maximum
    cognitive complexity 17, bounded iterative loop depth 2, no scan in loop
    and no recursion. The ledger preserves Changes 2-7 with eleven active rows
    and SHA-256 identity
    `8cc096a2ea6a05d97ae473f34c2bdc3a11a84f000e72fb95b4748f82ac4878fd`.
    Pinned formatting touched only changed `.cpp` files; public-header additions
    were semantic and formatting-only header churn was avoided. No Release
    qualification, sanitizer or hosted-CI inspection ran. Preserve Changes 1-7
    and proceed to Change 8 without reset, commit or push.
25. Batch 168 Change 8 is complete in the same intentionally dirty worktree.
    All accepted revisions transactionally lower cells and every delay,
    timing-check, timing-environment and label descendant into one immutable
    `shared_ptr<const SdfIr>` snapshot. Its deterministic pre-order vector
    retains contiguous node IDs, cell/parent/sibling/depth relations,
    exact/scaled values, canonical atoms, token intervals, physical spans,
    structural source IDs and revision-specific profile identity. Length-framed
    semantic serialization compares equivalent 2.1/3.0/4.0 fixtures equal while
    independently retaining revision, adapter, source and coordinates; the SDF
    2.1 physical-only wildcard remains distinct. Two cataloged diagnostics
    reject incomplete normalized state and cell/node/identity/depth resource
    exhaustion before any partial snapshot publishes. The warning-clean Debug
    targets, inventory, catalog and source gates pass 11/11 in 0.54 seconds.
    Graph audit reports maximum cognitive complexity 8, loop depth 1, no scan
    in loop and no recursion. The ledger preserves Changes 2-8 with ten active
    rows and SHA-256 identity
    `2f090531c6e8feeaa6c57256e77c9482cd437b11e7509bfe9a9b3039d94f51e1`.
    Pinned formatting touched only changed `.cpp` files; public-header additions
    were semantic and formatting-only header churn was avoided. No Release
    qualification, sanitizer or hosted-CI inspection ran. Preserve Changes 1-8
    and proceed to Change 9 without reset, commit or push.
26. Batch 168 Change 9 is complete in the same intentionally dirty worktree.
    A transactional annotation-scope binder selects exactly one elaborated
    root, an explicit root set or every root in canonical lexical order. Its
    immutable snapshot retains the normalized IR and semantic source identity,
    independent source provenance, optional DESIGN header, hierarchy divider,
    exact project/design identities, and each selected Verilog,
    SystemVerilog, VHDL or native-SystemC semantic unit, language and hierarchy
    case policy. Five cataloged diagnostics reject incomplete IR and identities,
    malformed/duplicate/missing roots, conflicting semantic roots,
    cross-project ownership, stale designs and governed root/identity
    exhaustion before publication. Equivalent SDF 2.1/3.0/4.0 and relocated
    sources produce the same scope identity; explicit root sets are
    request-order independent. The dedicated warning-clean Debug target plus
    dependent SDF, inventory, catalog and source gates pass 12/12 in 0.56
    seconds. Graph audit reports maximum cognitive complexity 16, loop depth
    1, no scan in loop and no recursion. The ledger preserves Changes 2-9 with
    nine active rows and SHA-256 identity
    `f1b381a55dada1ecbe77903370c38f9879ed38521d27f17afbf882b2f2e17bbd`.
    Pinned formatting touched only the new implementation/test `.cpp` files;
    the new semantic header received no formatting-only churn. No Release
    qualification, sanitizer or hosted-CI inspection ran. Preserve Changes 1-9
    and proceed to Change 10 without reset, commit or push.
27. Batch 168 Change 10 is complete in the same intentionally dirty worktree.
    Transactional cell resolution catalogs HDL specializations and native
    SystemC instances beneath the selected roots, reconstructs structural
    segments from semantic parents even when an instance label contains the
    hierarchy divider, and applies each owning parent's language case policy.
    Exact full and scope-relative selectors, empty-root selectors and lexical
    wildcards retain stable declaration, unit/configuration, root, path,
    language, case and physical-cell identity. VHDL configuration roots match
    their configured entity, while the SDF 2.1 wildcard alone retains its
    physical-cell restriction. Four cataloged diagnostics reject stale or
    incomplete scopes, missing and ambiguous cells, duplicate semantic owners,
    and candidate, match or identity exhaustion without partial publication;
    missing-cell messages include sorted actionable candidates and exact source
    coordinates. The warning-clean eight-worker Debug dependency build and 13
    focused application, frontend, inventory, catalog and source gates pass in
    0.55 seconds at 91,540 KiB peak RSS with zero swaps. Graph audit reports
    maximum cognitive complexity 18, loop depth 2, ten bounded string/path scan
    sites, no candidate-quadratic scan and no recursion. The ledger preserves
    Changes 2-10 with eight active rows and SHA-256 identity
    `bf574b388323960b4e1d74b41322155b285aedfff2f34c721c98b577c9def135`.
    Pinned formatting touched only the new implementation/test `.cpp` files;
    the new semantic header received no formatting-only churn. No Release
    qualification, sanitizer or hosted-CI inspection ran. Preserve Changes 1-10
    and proceed to Change 11 without reset, commit or push.
28. Batch 168 Change 11 is complete in the same intentionally dirty worktree.
    Transactional endpoint resolution binds port, net, interconnect, device,
    path, timing-check and identifiable condition endpoints across Verilog,
    SystemVerilog, VHDL/VITAL and native-SystemC proxy boundaries without
    applying timing. Immutable mappings retain the exact SDF node/cell and
    target instance, dense signal/object identity, language, direction,
    vector selector, edge and condition identity, boundary-conversion kind and
    peer, and uniquely linked retained specify-path or timing-check identity.
    Full and relative endpoint names follow the target language case policy;
    empty DEVICE selectors resolve the output-port set; deterministic node-ID
    lookup returns the complete equal-ID range. Five cataloged diagnostics
    reject incomplete/stale state, missing endpoints with candidates,
    ambiguity, undecodable endpoint-bearing constructs and governed node,
    candidate, mapping, endpoint or identity exhaustion transactionally. SDF
    2.1/3.0/4.0 resolved identities remain deterministic. The warning-clean
    eight-worker Debug build and 13 focused SDF/application/catalog/source/
    inventory gates pass in 0.45 seconds at 20,068 KiB peak RSS with zero
    swaps. Graph audit reports maximum cognitive complexity 17, local loop
    depth 2, four bounded scan-in-loop sites, indexed per-cell traversal and no
    recursion. The ledger preserves Changes 2-11 with seven active rows and
    SHA-256 identity
    `f8ec33725de8c7a321d31c9e6e97db88608aab7827a453310c0682dea5abe866`.
    Pinned formatting touched only implementation/test `.cpp` files; the new
    semantic public header received no formatting-only churn. No Release,
    sanitizer, hosted-CI inspection, reset, commit or push ran. Preserve
    Changes 1-11 and begin Change 12 whole-mapping validation in this worktree.
29. Batch 168 Change 12 is complete in the same intentionally dirty worktree.
    Transactional whole-mapping validation rechecks normalized-IR, cell,
    target, signal, native-SystemC object and conversion ownership and complete
    one-to-one consumption before publishing an immutable summary. It rejects
    nondeterministic order, duplicate node/target pairs, endpoints and semantic
    annotations, conflicting or unsupported object kinds, direction, width,
    bit/range selector, language and conversion state, wildcard/exact overlap,
    stale/unowned mappings and unconsumed constructs. The published summary
    retains its endpoint-resolution snapshot, canonical per-cell/target
    annotation/endpoint totals, sorted unique signal IDs, delay/timing-check/
    timing-environment counts and bounded semantic identity. Five cataloged
    diagnostics retain exact coordinates, and SDF 2.1/3.0/4.0 equivalent plus
    repeated mappings produce equal summaries. The warning-clean eight-worker
    Debug build and 14 focused SDF/application/catalog/source/inventory gates
    pass in 0.46 seconds at 20,164 KiB peak RSS with zero swaps. Graph audit
    reports maximum cognitive complexity 12, local loop depth 2, one bounded
    scan-in-loop site and no recursion. The ledger preserves Changes 2-12 with
    six active rows and SHA-256 identity
    `ee7136ce5cfe34fbd1e03df1549ac2f2c93a5e3bf1c31daf6ce4de30edd01ae1`.
    Pinned formatting touched only implementation/test `.cpp` files; the new
    semantic public header received no formatting-only churn. No Release,
    sanitizer, hosted-CI inspection, reset, commit or push ran. Preserve
    Changes 1-12 and begin Change 13 versioned SDF schemas in this worktree.
30. Batch 168 Change 13 is complete in the same intentionally dirty worktree.
    The ordered bounded binary schema envelope independently versions its
    envelope, syntax, normalized IR, endpoint resolution and mapping summary.
    It retains canonical revision/adapter, source and payload SHA-256 checksums,
    parse/normalization and compiler-compatibility identities, exact file and
    header spellings/spans/expansion provenance, immutable IR/resolution/
    summary identities and cell/node/mapping counts. Its all-or-nothing decoder
    validates every required ordered unique length-delimited record, enum,
    version, size, nested string/span, checksum and expected compatibility/
    mapping identity before publishing a snapshot. Five cataloged diagnostics
    cover incomplete/stale producers, future schemas, stale compatibility,
    corrupt/omitted/duplicate/reordered/truncated records and resource limits.
    Deterministic SDF 2.1/3.0/4.0, exact round-trip and all required negative
    classes pass. The warning-clean eight-worker Debug build and 15 focused
    SDF/application/catalog/source/inventory gates pass in 0.46 seconds at
    20,332 KiB peak RSS with zero swaps. Graph audit reports maximum cognitive
    complexity 20, local loop depth 2, one bounded scan-in-loop site and no
    recursion. The ledger preserves Changes 2-13 with five active rows and
    SHA-256 identity
    `930ccb69473c04471eda4f43b1bbfd7587fc7d99aeee895a644dd8d8bab88273`.
    Pinned formatting touched only implementation/test `.cpp` files; the
    semantic public header received no formatting-only churn. No Release,
    sanitizer, hosted-CI inspection, reset, commit or push ran. Preserve
    Changes 1-13 and begin Change 14 artifact/cache identity persistence.
31. Batch 168 Change 14 is complete in the same intentionally dirty worktree.
    Format-9 `.fsimdesign` metadata retains ordered SDF revision/adapter, exact
    optional timescale, min/typ/max policy, annotation scope, source/design
    digests, IR/resolution/mapping identities, selected-root identities,
    resolved semantic-unit identities and hashed semantic-object identities.
    Its transactional builder validates the complete schema-to-mapping owner
    chain and resource budgets, composes both design and specialization cache
    keys, and recomputes the portable artifact digest only after success.
    Dedicated evidence proves deterministic invalidation for source, revision,
    policy, hierarchy and selected-root edits; exact format-9 round trips;
    duplicate/stale/resource rejection; and identical cache identity after
    source-path relocation while the coordinate envelope retains the changed
    path. The warning-clean eight-worker Debug build and 17 focused SDF,
    artifact, catalog, source and inventory gates pass in 0.47 seconds at
    20,212 KiB peak RSS with zero swaps. Change-14-owned graph complexity peaks
    at cognitive 10 and loop depth 2 with no scan-in-loop or recursion; the
    factored legacy artifact decoder remains at its pre-change cognitive score
    56. The ledger preserves Changes 2-14 with four active rows and SHA-256
    identity
    `0a5ed4a27c4fb6f60ce4318dd09256dca637406d644659e25dd2d4f29702ccf0`.
    Formatting was confined to the new implementation/test `.cpp` files;
    semantic public headers and the legacy artifact implementation received no
    formatting-only churn. No Release, sanitizer, hosted-CI inspection, reset,
    commit or push ran. Preserve Changes 1-14 and begin Change 15 library,
    relocation and source-hidden SDF persistence.
32. Batch 168 Change 15 is complete in the same intentionally dirty worktree.
    A checksummed `FSDFPORT` schema-1 archive retains the exact SDF schema
    envelope, format-9 annotation identity, every normalized cell/node and
    every resolved cell/endpoint mapping. Mapping records retain target paths
    and IDs plus hashed semantic-unit/object identities, allowing consumers to
    verify and reconstruct immutable archived mappings without producer source
    or language-wide re-resolution. Revision/cache/profile-qualified mapped-
    library units and `sdf:` standalone-design payloads carry the same archive.
    Dedicated evidence publishes and relocates an analyzed library, consumes it
    after hiding the producer source, and publishes/reloads a standalone design;
    corrupt, stale, incompatible and governed-resource cases reject
    transactionally under four cataloged diagnostics. The warning-clean eight-
    worker Debug build and 19 focused SDF, project, library, design-artifact,
    catalog, source and inventory gates pass in 0.62 seconds at 20,180 KiB peak
    RSS with zero swaps. Change-15-owned graph complexity peaks at cognitive 10
    and loop depth 2 with one bounded generic decode scan-in-loop site and no
    recursion. The ledger preserves Changes 2-15 with three active rows and
    SHA-256 identity
    `13d5037c85186a377d5faf4cb115e0c022014ef6c8a52ebacf62a60bb29e7d6d`.
    Formatting was confined to the new implementation/test `.cpp` files;
    semantic public headers and legacy artifact sources received no formatting-
    only churn. No Release, sanitizer, hosted-CI inspection, reset, commit or
    push ran. Preserve Changes 1-15 and begin Change 16 direct/non-project,
    engine and cold/warm-cache SDF persistence.
33. Batch 168 Change 16 is complete in the same intentionally dirty worktree.
    Validated immutable SDF phase artifacts travel on the built-project owner,
    publish as indexed `sdf:` design payloads and restore transactionally before
    standalone simulation construction. Fresh annotations compose design and
    specialization cache identities once; restored archives retain that
    composition, and publication regenerates portable UVM bootstrap provenance
    against the composed cache. Production non-project evidence compiles and
    reloads portable HDL objects, resolves a real SDF interconnect, publishes
    base and annotated designs, hides the SDF source, relocates the design and
    reconstructs exact normalized/mapping state. Interpreter, LLVM cold/warm
    and relocated-warm runs all stop at the unchanged tick 3; missing, stale and
    corrupt SDF state rejects before execution. The warning-clean eight-worker
    Debug build and 20 focused SDF, phase, project, library, design-artifact,
    catalog, source and inventory gates pass in 31.54 seconds at 323,920 KiB
    peak RSS with zero swaps. Change-16-owned graph complexity peaks at
    cognitive 15 and loop depth 2 with three bounded scan-in-loop sites and no
    recursion; factored legacy publish/load functions are cognitive 34/33. The
    ledger preserves Changes 2-16 with two active rows and SHA-256 identity
    `b82fe415ba73daba25ce1002e47928bf7f25e5d96414b1a0e59db2d4957dd843`.
    Pinned formatting touched only the new implementation `.cpp`; the required
    semantic project-state header addition and legacy phase/test sources
    received no formatting-only churn. No Release, sanitizer, hosted-CI
    inspection, reset, commit or push ran. Preserve Changes 1-16 and begin
    Change 17 authoritative clean-room revision corpora.
34. Batch 168 Change 17 is complete in the same intentionally dirty worktree.
    Six checked-in clean-room files are indexed by SDF 2.1/3.0/4.0 with
    independent profile and mixed-resolution corpora. They cover ordered
    headers, divider-qualified names, exact signed decimal/triple/timescale
    values, exact/wildcard/empty cells, revision-legal condition/delay/timing-
    check/timing-environment constructs and immutable normalized identities.
    The mixed files resolve exact Verilog/native-SystemC plus wildcard physical
    VHDL cells and publish deterministic whole-mapping summaries. The dedicated
    runner emits `FSIM-SDF-CORPUS-PASS revisions=3 files=6 hierarchy=verilog-
    vhdl-systemc selectors=exact-wildcard`. The warning-clean eight-worker Debug
    build and 20 focused SDF, project, library, design-artifact, catalog, source
    and inventory gates pass in 0.53 seconds at 20,208 KiB peak RSS with zero
    swaps. Graph complexity peaks at cognitive 5 and loop depth 1 with no scan-
    in-loop or recursion. The ledger preserves Changes 2-17 with one active row
    and SHA-256 identity
    `17b890c8054656c85f847c6e37daea2532ecf1d885a762de9abe96417d727794`.
    Formatting touched only the new corpus-runner `.cpp`; no header or legacy
    source formatting ran. No Release, sanitizer, hosted-CI inspection, reset,
    commit or push ran. Preserve Changes 1-17 and begin Change 18 retained-log
    negative and differential closure.
35. Batch 168 Change 18 is complete in the same intentionally dirty worktree.
    The cross-platform retained-log harness runs every SDF frontend, adapter,
    normalization, IR, scope, resolution, mapping, schema, artifact, corpus,
    project/library/design, application-engine and resource-portability gate
    under a governed 1,200-second ceiling. It requires corpus, application,
    older-standard artifact and complete-CTest progress markers plus asserted
    lexical/parser/revision/cell/endpoint/mapping/schema/artifact/portable
    negative families, independently of subprocess exit status. The retained
    console covers interpreter/LLVM, cold/warm/relocated cache, portable
    artifacts, mixed Verilog/VHDL/SystemC, Linux execution and the Windows
    portability contract; it ends with `FSIM-SDF-CLOSURE-PASS engines=
    interpreter-llvm cache=cold-warm-relocated negatives=10 resources=governed`.
    The warning-clean eight-worker Debug build and closure pass in 32.51 seconds
    at 324,020 KiB peak RSS with zero swaps. The complete 17-row ledger has no
    active rows and SHA-256 identity
    `5a4817053834c76fef49650d38b73957a4be5560c62d1eb0f8c72c7180340c4b`.
    No source/header formatting ran. No Release, sanitizer, hosted-CI
    inspection, reset, commit or push ran. Preserve Changes 1-18 and begin
    Change 19 public documentation and evidence synchronization.
36. Batch 168 Change 19 is complete in the same intentionally dirty worktree.
    Public README, architecture, language, Verilog, VHDL/PSL, diagnostic,
    feature/evidence and release-audit surfaces now state one exact boundary:
    Batch 168 parses, normalizes, resolves and persists SDF without changing
    runtime timing; Batch 169 owns Verilog/SystemVerilog application and Batch
    170 owns VHDL/VITAL application. New installed `sdf.md` documents supported
    revisions, exact values, mixed hierarchy resolution, the source-tree C++
    phase APIs, `FSDFPORT` schema 1, mapped-library/design/cache persistence,
    retained evidence, and the absence of a timing-applying CLI switch.
    Synchronized release counts are 2,324 diagnostics, 925 bounded sources,
    1,083 SPDX-owned artifacts and 346 authored test/control files. Reviewed
    feature-matrix and VHDL/PSL release-closure SHA-256 identities are
    `ac8c85884b50854481f4eb68aa621b6b6b1ff080f00b0745fcf3046de7dba1b9`
    and
    `ae197625c75869bc8d60d4c7f66151f1196f78c22d2e566664dba62bd1bec1f0`;
    the complete 17-row SDF ledger remains
    `5a4817053834c76fef49650d38b73957a4be5560c62d1eb0f8c72c7180340c4b`.
    All six clean-room corpus files carry Apache-2.0 notices. Documentation,
    catalog, source, inventory, public/install, language-closure and composed
    release-candidate gates pass. No source/header formatting, Release build,
    sanitizer, hosted-CI inspection, reset, commit or push ran. Preserve
    Changes 1-19 and begin final Change 20 clean-first Debug/Release
    qualification.
37. Batch 168 Change 20 local qualification is complete. Fresh clean-first
    exact-LLVM 22.1.8 Debug and Release eight-worker builds complete all 824
    steps warning-free in 12:16.97 and 10:27.27 at 5,084,240 and 2,255,404 KiB
    peak RSS with zero swaps. Complete non-sanitized regressions pass 162/162
    in 11:19.96 and 10:30.22 wall time, with CTest totals 679.95 and 630.22
    seconds, peak RSS 3,782,884 and 3,794,928 KiB and zero swaps. SDF closure
    passes within the full runs in 30.48 and 26.15 seconds. Final pinned
    formatting touched only new `sdf_parser.cpp`; no header or legacy source
    formatting ran. Both incremental eight-worker rebuilds and post-format
    16/16 SDF/core Debug and Release slices pass in 32.03 and 26.93 seconds.
    Retained build/test transcripts under `build/change20-evidence/` contain no
    compiler warning/error or CTest-failure marker, repository whitespace is
    clean, and the refreshed graph contains 28,836 nodes and 135,445 edges.
    Release work occurred only at this final batch boundary. No sanitizer or
    hosted-CI inspection ran. Create and push the sole accumulated Batch 168
    implementation commit, then expand, save and push the exact Batch 169
    restart plan before beginning its implementation.

## Batch 167 planned restart checkpoint - 2026-08-13

1. Start in `/home/colin/projects/fsim`, read this section and the authoritative
   expanded Batch 167 allocation in `implementation_plan_v2.md`, and verify
   branch `codex/v2` is clean and synchronized at Batch 166 implementation
   `474abc34da5949b5bdb4e1457fc06e59f7d7366d` plus this documentation-only
   Batch 167 planning checkpoint.
2. This checkpoint implements the required pre-batch flow. Batch 167 is
   expanded into exactly twenty changes without broadening its locked older-
   Verilog/SystemVerilog-standard scope. No Batch 167 implementation file has
   changed; clear context after pushing this plan and resume only from this
   section and the authoritative allocation.
3. Preserve Batch 166's explicit VHDL-87/93/2000/2002 selection, typed source/
   dependency identity, revision-correct predefined environments, protected/
   shared semantics and clean-room non-standard Synopsys package declarations
   and arbitrary-width bodies beside the VHDL-2008 default.
4. Preserve Batch 166's `.fsimlib`, `.fsimobj`, `.fsimdesign`, LLVM cache,
   semantic-unit provenance, mixed-language, debugger/VHPI/VCD, checkpoint,
   relocation, replay and non-project evidence. Final standard/package ledger
   identities are
   `9a97ed1964d3fd24aa4644f85dcada8bf114fc6bce42001bd092e02e9bbd8434`
   and
   `118ee444e373f2fc46624bd17fb7c9d2664c838f647f7d75bae59a10eea9f6b0`.
5. Batch 166's fresh clean-first exact-LLVM 22.1.8 Debug and Release
   eight-worker builds complete 781 steps warning-free in 12:03.44 and 9:48.97
   at 5,015,728 and 2,253,856 KiB peak RSS with zero swaps.
6. The first complete Debug run exposed an analysis-order fixture using VHDL-
   2008 reserved word `shared` as a context identifier. The legal
   `analysis_context` repair and diagnostic-on-unexpected-failure output pass
   three focused runs in each configuration. Complete Debug and Release reruns
   pass 144/144 in 12:12.23 and 10:24.45 wall time, with CTest totals of 732.22
   and 624.44 seconds, peak RSS of 3,786,880 and 3,791,172 KiB, and zero swaps.
7. Final post-documentation static/release slices pass 41/41 in 21.84 and 21.02
   seconds. New Batch 166 C++ sources and the exact correction pass pinned
   clang-format 22.1.8; repository whitespace is clean. Batch 166 was neither
   a sanitizer nor hosted-CI monitoring boundary and is pushed as `474abc3`.
8. The synchronized Batch 166 baseline is 2,236 production diagnostics, 887
   bounded C/C++ sources, 1,029 SPDX-scoped artifacts, 332 authored test/control
   files, 1,294 feature rows, 5,176 evidence cells and 617 exact evidence paths.
   Feature and evidence SHA-256 identities are
   `5f6a4d99889db39a81694c8d4ee585bdb0cdffaea577ddf275e5d3e1eca1cd4f`
   and
   `e4076a40c31945c3385495a1c3076b85518143712c89ea1fa3f629fffaac6120`.
9. Batch 167 adds explicit Verilog-1995, Verilog-2001,
   Verilog-2001-noconfig, SystemVerilog-2005, SystemVerilog-2009 and
   SystemVerilog-2012 modes while preserving Verilog-2005 and
   SystemVerilog-2017 as the default complete baselines.
10. A selected older mode controls preprocessing, keywords, grammar, semantic
    defaults, predefined services, execution and all persistent/public
    boundaries. It may not be a parser-only label. Explicit compatibility
    switches must be deterministic, provenance-bearing and independently
    cache-keyed; they may not silently enable unrelated newer grammar.
11. Changes 1-4 freeze revision/switch inventories, expose canonical manifest/
    CLI/Tcl identities, carry source/library dependency identity and key
    preprocessing, artifacts and native caches with stable mismatch diagnostics.
12. Changes 5-8 close preprocessing/lexical, declaration/type/port,
    expression/assignment/process and hierarchy/generate/configuration/
    assertion/class/interface/package legality by revision. The noconfig mode
    differs from ordinary Verilog-2001 only by disabling configuration syntax
    and semantics.
13. Changes 9-12 provide revision-correct predefined environments, system
    tasks/functions, DPI/VPI surfaces, semantic defaults and explicit
    compatibility switches while preserving arbitrary widths and exact
    four-state behavior.
14. Changes 13-16 preserve selected revision/switch provenance through
    libraries/artifacts, interpreter/LLVM/mixed execution, C/C++/Tcl/debugger/
    VPI/VCD surfaces, checkpoints, relocation, replay, caches and non-project
    phases.
15. Changes 17-19 publish standard/switch-specific positive and negative
    corpora and a retained-log cross-engine/platform matrix, then synchronize
    public documentation, diagnostics, inventories, counts, digests and this
    handoff.
16. Change 20 runs fresh clean-first exact-LLVM Debug/Release builds with at
    least eight workers and 120-minute command timeouts, full non-sanitized
    regressions and every release gate. Retain timing/RSS/swap/transcript
    evidence and create the sole Batch 167 implementation commit/push only
    after all local gates pass. Batch 167 is not a hosted-CI monitoring
    boundary.
17. Accumulate Changes 1-20 in one intentionally dirty worktree. Do not reset,
    commit or push implementation before Change 20. After that implementation
    commit, save and push the exact Batch 168 restart plan and clear context
    before Batch 168 implementation.
18. Batch 167 Change 1 is complete in the intentionally dirty worktree. The
    registered planning contract governs two 17-row active ledgers assigned
    one-to-one to Changes 2-18. The standard-mode ledger spans all six older
    revisions through selection, preprocessing, language semantics, services,
    execution, persistence, public provenance, corpora and closure. The
    compatibility ledger independently governs keyword-profile, implicit-net,
    port-connection, sizing, lifetime, scheduler/assertion and configuration
    switches at the same boundaries. Every row has exact existing owner paths;
    missing, duplicate, reordered, misplaced or drifted ownership fails. Their
    SHA-256 identities are
    `4522bb8b618f79cd7c60cbe77c4842c891189450723a6f7278fbfa91ad37ebbf`
    and
    `4cb6f89b24176bb58715acab291128397345d5022445919b100f43e22dc0e549`.
    The live baseline remains 2,236 diagnostics, 887 bounded sources and 332
    authored test/control files while SPDX ownership advances to 1,032.
    Preserve Change 1 and begin Change 2 without reset, commit, push, sanitizer
    or hosted-CI inspection before Change 20.
19. Batch 167 Change 2 is complete in the same intentionally dirty worktree.
    Public typed Verilog-1995/2001/2001-noconfig/2005 and SystemVerilog-2005/
    2009/2012/2017 identities now canonicalize documented manifest, source-set
    and direct-CLI aliases; absent selection retains the Verilog-2005 and
    SystemVerilog-2017 defaults. CLI help publishes the families, project
    negatives retain source-owned diagnostics, and Tcl round-trips canonical
    Verilog-2001-noconfig and SystemVerilog-2009 profiles. Debug and Release
    project/application/Tcl slices pass 3/3 in 33.43 and 27.92 seconds. The
    inventories preserve Change 2 with sixteen active rows and identities
    `c3224bf29f50e3cac3dca089e0d4c28f4beefaead606d1d6716e6ed580908f8b`
    and
    `aada15d0b3dbf10667f4dbea4805987e87a10d1756ba63fb68a3ce2fea402354`.
    Preserve Changes 1-2 and begin Change 3 without reset, commit, push,
    sanitizer or hosted-CI inspection before Change 20.
20. During Changes 1-19, run focused and dependent Debug validation only.
    Release build/testing is not required until the final Change 20 batch
    checks. Avoid header-only formatting edits that induce broad rebuilds;
    format implementation hunks without churning otherwise unchanged public
    headers. An in-progress Change 3 Release rebuild was stopped immediately
    when this policy was recorded; its partial output is not qualification
    evidence.
21. Batch 167 Change 3 is complete in the same intentionally dirty worktree.
    Typed revision identity now spans parse groups, checked roots/includes,
    analyzed units, UDPs, class declarations/methods and mapped-library
    provenance. Library metadata canonicalizes language/revision pairs.
    `FSIM-FE-STANDARD-001` through `003` reject incompatible physical source or
    include reuse, unit reanalysis and SystemVerilog package consumption at the
    owning source boundary. Debug frontend/application/conformance/diagnostic/
    source/inventory evidence passes 6/6 in 34.74 seconds. The inventories now
    preserve Changes 2-3, retain fifteen active rows and have identities
    `147c389b468f30b68b8e9f6846dfdf9c9a181dc2011a39a89770e0a9d07499d1`
    and `e17ce02c674e79fa607d3373a6f1c4504f4fe45e50eb512d4f5b653d1147bc13`.
    Preserve Changes 1-3 and begin Change 4 without reset, commit, push,
    Release qualification, sanitizer or hosted-CI inspection before Change 20.
22. Batch 167 Change 4 is complete in the same intentionally dirty worktree.
    Manifest source sets and direct CLI compile selection canonicalize the seven
    governed compatibility switches independently of the selected revision.
    The profile now enters preprocessing compilation-unit, project,
    semantic-dependency, specialization and LLVM persistent cache identities;
    portable objects/designs round-trip it and object reload reconstructs the
    source settings. Current formats needed no schema bump, and their existing
    future-format rejection remains strict. Focused Debug project, design,
    LLVM and artifact-phase tests pass 4/4; the LLVM owner takes 50.63 seconds
    and the final artifact rerun 0.98 seconds. No Release qualification was
    run. Both inventories preserve Changes 2-4, retain fourteen active rows and
    have identities
    `59b7716757a9424038f3620afb9bd158bfe8766c1ad31f29f7b56078a7e2abdd`
    and `4682db49b6958e2ef1b775ed3e6617d20d8e0033bed43e7f8c183d21b86529f9`.
    Preserve Changes 1-4 and begin Change 5 without reset, commit, push,
    Release qualification, sanitizer or hosted-CI inspection before Change 20.
23. Batch 167 Change 5 is complete in the same intentionally dirty worktree.
    The typed selected revision now controls preprocessing and parser keyword
    initialization. Roots/includes retain revision provenance; nested
    `begin_keywords` regions cannot select a later set. SystemVerilog-only
    directives, predefined macros, default arguments, token concatenation,
    stringification, unbased literals, time-unit literals and string escapes
    receive exact-span `FSIM-SV-PP-052` diagnostics in older modes, while a
    Verilog-1995 include preserves an escaped identifier and exact 257-bit
    literal. The required semantic header edit caused one 201-step eight-worker
    Debug dependency rebuild, not formatting churn; it completed warning-clean.
    The focused Debug frontend/diagnostic/source/inventory/preprocessing/
    conformance slice passes 6/6 in 5.80 seconds. No Release qualification was
    run. Both ledgers preserve Changes 2-5, retain thirteen active rows and
    have identities
    `b15afb11a3b959145bf180c1ea227bc86a5b35c7a21f23dcebdcc356f6ebf03f`
    and
    `d90a908343aafbd05b3040643d96fffee84d4f4f2c05e6f584dc1844114a5aae`.
    Preserve Changes 1-5 and begin Change 6 without reset, commit, push,
    Release qualification, sanitizer or hosted-CI inspection before Change 20.
24. Batch 167 Change 6 is complete in the same intentionally dirty worktree.
    Typed parser revision gates now cover Verilog-1995 non-ANSI versus
    Verilog-2001 ANSI/parameter-port/signed/localparam/initializer forms,
    Verilog-2005 `uwire`, SystemVerilog-2005 typed/interface/unpacked ports,
    type parameters, lifetimes and program headers, SystemVerilog-2009 checker
    headers, and SystemVerilog-2012 `nettype`. Later declaration words are
    rejected at their owning token with `FSIM-SV-PARSE-346`; legal variable
    initializers become explicit initial-process HIR while net initializers
    remain continuous drivers. The inventory-owned aggregate application test
    and independent conformance application both prove source-set revision
    behavior. The focused Debug frontend/diagnostic/source/inventory/aggregate/
    conformance slice passes 6/6 in 16.10 seconds. No Release qualification was
    run. Both ledgers preserve Changes 2-6, retain twelve active rows and have
    identities
    `9377cbbe2eabc9262f41dabcaed79d594bb011f0cd4f06f0b6bb316678e64a02`
    and
    `ef23289e5b5ff204e5d78753834857b3074533d777da823595b3c5ff435e71ab`.
    Preserve Changes 1-6 and begin Change 7 without reset, commit, push,
    Release qualification, sanitizer or hosted-CI inspection before Change 20.
25. Batch 167 Change 7 is complete in the same intentionally dirty worktree.
    `FSIM-SV-PARSE-347` now gates Verilog-2001 operators/indexed selects/event
    expressions and ANSI callable headers, SystemVerilog-2005 casts/patterns/
    streaming/inside/wildcard and update/process/control forms, and the
    SystemVerilog-2009 `unique0` boundary. Verilog-1995 retains exact
    arbitrary-width four-state legacy expressions and processes. The
    inventory-owned sizing application executes a Verilog-2001 257-bit power,
    arithmetic-shift and indexed-select process in interpreter and compiled
    engines and rejects a compound update under Verilog-1995. The source-policy
    gate remains green after consolidating only the new implementation-local
    checks; no public-header formatting churn was introduced. The focused Debug
    frontend/diagnostic/source/inventory/execution slice passes 5/5 in 3.87
    seconds. No Release qualification was run. Both ledgers preserve Changes
    2-7, retain eleven active rows and have identities
    `1c7f3908f73ada752e73b8c1cc578b47d26f90ad59b027793c82b6102462e98d`
    and
    `2ff1f65714064872944c60ad30552c7c1d5cae14c99e2482949235d427ecc3d3`.
    Preserve Changes 1-7 and begin Change 8 without reset, commit, push,
    Release qualification, sanitizer or hosted-CI inspection before Change 20.
26. Batch 167 Change 8 is complete in the same intentionally dirty worktree.
    `FSIM-SV-PARSE-348` now gates hierarchy, configuration, generate, bind,
    assertion/coverage, class/constraint, interface/modport, package/import and
    compilation-unit structure at owning tokens. Ordinary Verilog-2001 and
    Verilog-2005 retain configurations; only `verilog-2001-noconfig` disables
    them. SystemVerilog-2005, 2009 and 2012 structural introductions remain
    separated, including `let`, checker, interface-class and `implements`
    boundaries. Package, configuration and bind units retain typed revision
    provenance. The inventory-owned hierarchy application executes a legal
    Verilog-2001 configuration in interpreter and compiled engines and rejects
    the same source under the no-config profile through the public project API.
    No public header change or header-only formatting churn was introduced. The
    focused Debug frontend/diagnostic/source/inventory/hierarchy slice passes
    5/5 in 0.74 seconds. No Release qualification was run. Both ledgers preserve
    Changes 2-8, retain ten active rows and have identities
    `3c4817890c5384280203707abef5fd10d5013e70c4105f5a729a1f41cac59ed8`
    and
    `931af07dc4b28d58f778e8f3f36a185747bc8f0b3959f245ba2a2745889d6d30`.
    Preserve Changes 1-8 and begin Change 9 without reset, commit, push,
    Release qualification, sanitizer or hosted-CI inspection before Change 20.
27. Batch 167 Change 9 is complete in the same intentionally dirty worktree.
    `FSIM-SV-PARSE-349` now gates predefined SystemVerilog scopes, values,
    synchronization types and standard methods, as well as 2009 property
    operators and 2012 soft constraints, without consuming legal same-spelled
    identifiers in older profiles. Verilog-1995 exact integer/time/real/reg
    profiles and SystemVerilog-2005 exact integral/scalar/string/process/
    container profiles have typed frontend evidence. The inventory-owned
    conformance application runs explicitly as SystemVerilog-2005 through
    interpreter and compiled O0/O2 engines; its public-API negative rejects
    SystemVerilog predefined names under Verilog-2005. No public header change
    or header-only formatting churn was introduced. The focused Debug frontend/
    diagnostic/source/inventory/conformance slice passes 5/5 in 4.69 seconds.
    No Release qualification was run. Both ledgers preserve Changes 2-9,
    retain nine active rows and have identities
    `0c0cf9eeb396591b40e9266ce7d10fddd432a3119375b19cd640103059d81858`
    and
    `0857b21e7a8f23300f8108d5fa0d776c5534c43042eccc36137b47a7b8ada231`.
    Preserve Changes 1-9 and begin Change 10 without reset, commit, push,
    Release qualification, sanitizer or hosted-CI inspection before Change 20.
28. Batch 167 Change 10 is complete in the same intentionally dirty worktree.
    `FSIM-SV-PARSE-350` gates revision-specific system services and the later
    two-argument `$fopen` signature. The parser retains exact file, formatting,
    random, time, coverage, introspection, severity and active/postponed
    scheduling profiles across Verilog-1995/2001/2005 and
    SystemVerilog-2005/2009. The inventory-owned file application executes its
    complete corpus explicitly as SystemVerilog-2005 through interpreter and
    compiled O0/O2 engines and rejects a 2009-only global sampled service
    through the public project API. No public header change or header-only
    formatting churn was introduced. The focused Debug frontend, diagnostic,
    source, inventory, documentation and file-execution slice passes 6/6 in
    3.06 seconds. No Release qualification was run. Both ledgers preserve
    Changes 2-10, retain eight active rows and have identities
    `3778e114b1a713559919ae212e73fc3d2beee5677ae4032a2888d11b4580e9ce`
    and
    `03966dbae7af1bff7195f7748d31bd5802ec5652f55dec89e1485c60528ebe27`.
    Preserve Changes 1-10 and begin Change 11 without reset, commit, push,
    Release qualification, sanitizer or hosted-CI inspection before Change 20.
29. Batch 167 Change 11 is complete in the same intentionally dirty worktree.
    `FSIM-SV-PARSE-351` rejects DPI declarations in Verilog profiles and every
    accepted declaration retains its selected SystemVerilog revision. VPI
    publication now carries exact Verilog-1995/2001/2001-noconfig/2005 and
    SystemVerilog-2005/2009/2012/2017 identities while retaining ABI values
    zero and one for the pre-existing Verilog-2005 and SystemVerilog-2017 enum
    members. Unit revisions are captured before the parser workspace moves and
    are resolved through canonical HIR identities. Runtime validation still
    blocks SystemVerilog-only object kinds and descriptors under Verilog.
    Application evidence preserves a 129-bit four-state Verilog-1995 value,
    prevents later object leakage, and exercises SystemVerilog-2005 hierarchy,
    traversal and assertion callbacks. The required public-header edits were
    semantic only; no header formatting churn was introduced. The focused
    Debug frontend, runtime, application, diagnostic, source, inventory and
    documentation slice passes 7/7 in 0.81 seconds. No Release qualification
    was run. Both ledgers preserve Changes 2-11 with seven active rows and
    identities
    `be7df34aa7dbbf2f3c7b9e452c3daa1ca2cc87ee22c00d38c114fce3da923783`
    and
    `e77dca4a6ee10e1403a6e2ee08b47cb773b9e32bbc15cdf06af8b7a552de958b`.
    Preserve Changes 1-11 and begin Change 12 without reset, commit, push,
    Release qualification, sanitizer or hosted-CI inspection before Change 20.
30. Batch 167 Change 12 is complete in the same intentionally dirty worktree.
    Canonical compatibility identity now reaches parser entry and each produced
    Verilog/SystemVerilog design unit. `keyword-profile` selects only its
    governed legacy keyword set, and `configuration` restores only the optional
    Verilog-2001 configuration surface under the no-config revision. The full
    seven-switch profile retains an exact 257-bit declaration through direct,
    preprocessed and public project/application paths while still rejecting
    later `nettype` grammar. The one required semantic design-unit header field
    caused a warning-clean eight-worker Debug dependency rebuild; no header
    formatting churn was introduced. The final focused Debug project,
    frontend, application, diagnostic, source, inventory and documentation
    slice passes 7/7 in 4.69 seconds. No Release qualification was run. Both
    ledgers preserve Changes 2-12 with six active rows and identities
    `bfce8ca1662041f1f38492f29f335c1c9b7a4915c2bd72967d4418222dccbc92`
    and
    `c0fe9e5be1e5607fffda1f84b828bee80149c753545f047ff4bba123c55ee95f`.
    Preserve Changes 1-12 and begin Change 13 without reset, commit, push,
    Release qualification, sanitizer or hosted-CI inspection before Change 20.
31. Batch 167 Change 13 is complete in the same intentionally dirty worktree.
    Library format 3, portable schema 10 and owning-unit schema 26 retain exact
    language, revision and compatibility identity per source, design unit,
    UDP, class, nested class and method. Object format 5 binds ordered source/
    include checksums and unit profiles into its compilation digest. Design
    format 8 records ordered unique Verilog/SystemVerilog semantic-unit
    provenance matched to contributing objects and restores exact standalone
    unit maps. Mapped libraries reconstruct distinct settings for each retained
    revision/profile pair, including source-hidden units, and do not replace
    archived identities with language-wide defaults. Stale, omitted, duplicate,
    reordered and payload-incompatible identities reject before semantic
    publication. The required semantic header additions caused one expected
    eight-worker Debug dependency rebuild without formatting-only header churn.
    The final library, object, design, application, diagnostic, source,
    inventory and documentation Debug slice passes 9/9 in 29.87 seconds. No
    Release qualification was run. Both ledgers preserve Changes 2-13 with five
    active rows and identities
    `9ae0d0fa0a8c270e8116a753b8a550a66f3601c1512d57318f9bcd5d342716c8`
    and
    `5304a4c918cf692109e485c1341a3a0add38ef9c65f0d3cc176e9ef250036c78`.
    Preserve Changes 1-13 and begin Change 14 without reset, commit, push,
    Release qualification, sanitizer or hosted-CI inspection before Change 20.
32. Batch 167 Change 14 is complete in the same intentionally dirty worktree.
    The typed-boundary matrix executes Verilog-1995/2001/2001-noconfig and
    SystemVerilog-2005/2009/2012 with the full canonical seven-switch profile
    through interpreter and LLVM O0/O2, VHDL-2008 and native SystemC. An
    independent SystemVerilog-2017 `none`-profile root shares the design without
    identity contamination; all six specialization-key sets remain distinct.
    Exact 137-bit X/Z planes, specify timing, VHDL PSL observation, one-
    nanosecond stop time, debugger values, VCD changes and signal-change order
    agree between engines. Existing conversion and hierarchy evidence covers
    signedness/state adapters, configurations and binds. No production or
    public-header change was required. The final typed-boundary, mixed-
    conversion, hierarchy, diagnostic, source, inventory and documentation
    Debug slice passes 7/7 in 24.04 seconds. No Release qualification was run.
    Both ledgers preserve Changes 2-14 with four active rows and identities
    `e6089afd9632761a62ead0ad86feb7d979fcfae2e039c9b721710ea11679c5c0`
    and
    `155cac3a32046609f26cdf3fc321f0b37ecf1341fd26c3efbc2436dce6c6bfc8`.
    Preserve Changes 1-14 and begin Change 15 without reset, commit, push,
    Release qualification, sanitizer or hosted-CI inspection before Change 20.
33. Batch 167 Change 15 is complete in the same intentionally dirty worktree.
    A single canonical semantic projection now exposes owning unit/source,
    language/revision and compatibility profile through the append-only C API,
    C++, Tcl, debugger, VPI, detailed safe-point callbacks and filtered VCD
    comments. Descendant lookup selects the longest owning scope. Compiler,
    cache and specialization details remain absent from hierarchy discovery;
    public standard-conflict diagnostics name both incompatible revisions and
    profiles. Interpreter and LLVM O0/O2 evidence covers all six older modes,
    exact public identities, callback lifetime, trace filtering, partial VPI
    rejection and profile-conflict diagnostics. The required semantic header
    additions caused one expected eight-worker Debug dependency rebuild without
    formatting-only header churn. The final 11-test Debug slice passes, with
    the application gate completing in 30.30 seconds and the corrected
    catalog/documentation rerun passing 2/2 in 0.17 seconds. No Release
    qualification was run. Both ledgers preserve Changes 2-15 with three active
    rows and identities
    `aaac248cd203dd166f1f333027d8da9a5a3e359a2f81280d436f769e9ac6188b`
    and
    `f3ff1fd961769697edfeed87c1a51770d29a9d71acc5bce3a96996856fb49a71`.
    Preserve Changes 1-15 and begin Change 16 without reset, commit, push,
    Release qualification, sanitizer or hosted-CI inspection before Change 20.
34. Batch 167 Change 16 is complete in the same intentionally dirty worktree.
    The existing format-5 object, format-8 design and portable state schemas
    preserve revision/profile identity through direct non-project phases,
    interpreter and LLVM, cold/warm native caches, checkpoint serialization/
    replay and relocation. A new six-mode matrix covers Verilog-1995,
    Verilog-2001, Verilog-2001-noconfig and SystemVerilog-2005/2009/2012 with
    explicit `sizing`. It hides the original source and producer object before
    standalone reload, compares exact semantic unit/source IDs, specialization
    keys and checkpoint bytes, relocates the design, and repeats both engines.
    The independent exact-width path retains 137-bit X/Z and signed values,
    cache hits and filtered provenance VCD comments. Direct object builds and
    standalone designs agree; partial, stale and omitted provenance reject at
    their owning validation boundary. No production or header change was
    needed. The final Debug slice passes 9/9 in 29.80 seconds and its focused
    six-mode artifact gate passes in 1.12 seconds. No Release qualification was
    run. Both ledgers preserve Changes 2-16 with two active rows and identities
    `a436c2d76d0d8ef6da11f0a9b743c897111fde303301f1e684f96cccff68c77c`
    and
    `986d2088acf64c503f74c8f946c67099db4008f95d34059282c39ebad8924f69`.
    Preserve Changes 1-16 and begin Change 17 without reset, commit, push,
    Release qualification, sanitizer or hosted-CI inspection before Change 20.
35. Batch 167 Change 17 is complete in the same intentionally dirty worktree.
    Two authoritative TSV corpora publish ordered rows for all six older
    revisions and all seven explicit compatibility switches. Every row binds
    positive/negative source anchors, an exact diagnostic coordinate,
    execution, arbitrary-width, include/profile provenance and artifact
    mismatch evidence. The frontend asserts the six representative triples and
    tests every switch independently with an exact 257-bit declaration and a
    common later-grammar rejection. The registered inventory validates every
    row and anchor. The final eight-test Debug slice passes; the corrected
    diagnostic/inventory/documentation rerun passes 3/3 in 0.15 seconds. No
    production header changed and no Release qualification was run. The mode
    and compatibility ledgers preserve Changes 2-17 with one active row and
    identities
    `9627cda0c1a0d8e43e7f91061d0ea0cb1c67ca8620f344e9069b51c4eabc0bec`
    and
    `8fb74c03932536b3dc46cd8729cb10d8ac608da703b99292e19e86339b59cb08`;
    the revision and switch corpora have identities
    `8c3018215c3e8c7d1fdc90c275d2f3d3e4ee065956c8b205fb740a8d750981f7`
    and
    `34eca16ab78a6e2ab73f1e7d77cb36091fad5571b89f4ba5f8c512d69355a24a`.
    Preserve Changes 1-17 and begin Change 18 without reset, commit, push,
    Release qualification, sanitizer or hosted-CI inspection before Change 20.
36. Batch 167 Change 18 is complete in the same intentionally dirty worktree.
    A separate serial standard-mode closure matrix runs 16 inventory, frontend,
    application, engine/cache, artifact/replay, mixed-language, public API,
    runtime and platform/resource witnesses. It retains a 17-row stage ledger
    and one nonempty verbose log per witness, and requires exact six-mode,
    profile, 137-bit X/Z, engine, hidden-producer and resource transcript
    tokens. The Debug matrix passes 16/16 in 76.52 seconds. No source/header
    rebuild occurred; the MSVC Release contract was only a static configuration
    audit, and no Release build or executable test ran. Both ledgers preserve
    Changes 2-18 with zero active rows and identities
    `21a05e4732aa8da8b546f2c7048ee2b5d51cdca4572114fb5c9946d4bfad8d3e`
    and
    `5e7a74d4f9c0af29e28c4d0b9b35b96a5c9df6482b53759633f80904812c5f6f`.
    Preserve Changes 1-18 and begin Change 19 without reset, commit, push,
    Release qualification, sanitizer or hosted-CI inspection before Change 20.
37. Batch 167 Change 19 is complete in the same intentionally dirty worktree.
    Diagnostics, language/feature support, architecture, VPI/public guidance,
    evidence, inventories, closure registration and this handoff now describe
    one bounded contract: 17+17 preserved rows with zero active obligations,
    six revision and seven switch corpus rows, and 16 retained-log witnesses.
    Compatibility switches remain explicit semantic choices, not historical
    tool-emulation claims or later-grammar permission. The live baseline is
    2,246 diagnostics, 887 bounded sources, 1,035 SPDX-owned files and 332
    authored test/control files; the resulting VHDL/PSL closure digest is
    `0a71ce9863b488172f80b02c4cd82bc989c440e02aea996504598d5a65540fee`.
    The standard-mode runner and retained stage ledger identities are
    `fc8807632dc82722d962cc789a31b2be7dbfd2a31436354a9be61351c1943046`
    and
    `4179ca05b19cd5c22782d9a959f8db8bebee02841085c4745a870f0d952c7672`.
    The final static slice passes 10/10 in 0.98 seconds. CMake regeneration
    induced no source rebuild, no header formatting changed, and no Release
    build or executable test ran. Preserve Changes 1-19 and begin final Change
    20 qualification without reset, commit, push, sanitizer or hosted-CI
    inspection.
38. Batch 167 Change 20 is complete. Fresh clean-first exact-LLVM 22.1.8 Debug
    and Release eight-worker builds complete 781/781 steps warning-free in
    11:55.31 and 9:36.37 at 5,030,336 and 2,254,716 KiB peak RSS with zero
    swaps. Complete non-sanitized Debug and Release regressions pass 146/146 in
    9:11.10 and 7:59.35 wall time, with CTest totals of 551.08 and 479.34
    seconds, peak RSS of 3,781,532 and 3,750,768 KiB, and zero swaps. The first
    clean Debug qualification exposed and closed a false SystemVerilog time-
    literal rejection of legal Verilog `timescale` units, stale elaboration
    fixtures that bypassed semantic system-function arity ownership, Verilog
    library export/import metadata labeled as SystemVerilog, and a stale v1
    feature-matrix digest. The focused repair set passes 13/13 before the full
    reruns. Pinned clang-format 22.1.8 was limited to changed implementation and
    test `.cpp` files; no public header formatting churn was introduced.
    Release qualification was deferred to and run only for this final batch
    check. Batch 167 runs neither sanitizer nor hosted-CI inspection. Final
    post-documentation Debug and Release static/release slices pass 42/42 in
    22.84 and 21.63 seconds and `git diff --check` is clean. Create and push the
    sole accumulated Batch 167 implementation commit, then save and push the
    exact expanded Batch 168 restart plan as a documentation-only precursor and
    clear context before implementation.

## Batch 166 planned restart checkpoint - 2026-08-12

1. Start in `/home/colin/projects/fsim`, read this section and the authoritative
   expanded Batch 166 allocation in `implementation_plan_v2.md`, and verify
   branch `codex/v2` is clean and synchronized at Batch 165 implementation
   `d62bfafea0ae778986d5df41e00030afe5f7b8f3` plus this documentation-only
   Batch 166 planning checkpoint.
2. This checkpoint implements the required pre-batch flow. Batch 166 is
   expanded into exactly twenty changes without broadening its locked older-
   VHDL-standard and Synopsys-package compatibility scope. No Batch 166
   implementation file has changed; clear context after pushing this plan and
   resume only from this section and the authoritative allocation.
3. Preserve Batch 165's complete IEEE 1800-2017 residual-language and
   arbitrary-width closure: frontend/HIR/DesignIR/SimIR, interpreter/LLVM,
   DPI/VPI/UVM, mixed/public/debug/VCD boundaries, artifacts, checkpoints,
   libraries, relocation, replay and caches at pushed commit `d62bfaf`.
4. Batch 165's fresh clean-first exact-LLVM 22.1.8 Debug and Release
   eight-worker builds complete 779 steps warning-free in 11:05.59 and 9:10.78
   at 4,997,832 and 2,250,920 KiB peak RSS with zero swaps.
5. Complete Debug and Release regressions pass 142/142 in 7:51.79 and 6:30.42
   wall time, with CTest totals of 471.79 and 390.42 seconds, peak RSS of
   3,788,408 and 3,793,688 KiB, and zero swaps. Final post-documentation
   release-contract slices pass 34/34 in both configurations; exact Change 20
   correction ranges pass the WebKit gate and repository whitespace is clean.
6. Preserve the Change 20 validator repair: active-region program clocking
   support and sampler processes legitimately retain `program_owner`; do not
   restore the blanket reactive-region invariant. A fixture that executes
   `$finish` correctly reports `RunStatus::stopped` while retaining exact
   output. Direct live-state reconstruction and artifact round-trip must remain
   green.
7. The synchronized Batch 165 baseline is 2,228 production diagnostics, 885
   bounded C/C++ sources, 1,021 SPDX-owned files, 330 authored test/control
   files, 1,294 executable feature rows, 5,176 evidence cells, 617 exact
   evidence paths and 144 runtime owners.
8. SystemVerilog gap, literal-width and release-closure SHA-256 identities are
   `a2f19c56e715ea0f8198a672d96d08d0d9accd8eb7569f16bc6e542fc294ff40`,
   `f661b219e251e6369750ab406b19adf9c193cfb9570baaa0fdeab4f7984bad93`
   and
   `f4e8dcdfb60362544e6958449fa2a1e852cedcbba2ef6e929d5fb0a3aee1d124`.
   Feature and canonical-evidence identities are
   `b2c59a958173da718dacc8b02b4480e1a02efd4fcfed87f64a234ec4622d2e11`
   and
   `e4076a40c31945c3385495a1c3076b85518143712c89ea1fa3f629fffaac6120`.
9. Batch 166 adds explicit VHDL-87, VHDL-93, VHDL-2000 and VHDL-2002 modes
   while preserving VHDL-2008 as the default and complete baseline. Revision
   identity must survive selection, source analysis, dependencies, libraries,
   artifacts, caches, relocation, replay, public boundaries and mixed-language
   execution.
10. The compiler must supply the explicitly non-standard Synopsys
    `ieee.std_logic_signed`, `ieee.std_logic_unsigned`,
    `ieee.std_logic_arith` and `ieee.std_logic_misc` packages under their
    historical logical-library names. Their declarations and executable bodies
    must preserve arbitrary widths, null vectors, direction, unknowns and
    deterministic overload behavior beside standard `numeric_std`.
11. Changes 1-4 freeze the standard/package inventories, expose canonical
    manifest/CLI/Tcl identities, carry source/library revision identity and key
    artifacts/caches with deterministic mismatch diagnostics.
12. Changes 5-8 close lexical, declaration/type, expression/association/
    subprogram and package/configuration/generate/statement legality by
    revision, with actionable newer-feature diagnostics.
13. Changes 9-12 provide revision-correct predefined environments and
    protected/shared semantics, then ship all four Synopsys package declaration
    profiles and arbitrary-width executable bodies with explicit provenance
    and mixed-package ambiguity handling.
14. Changes 13-16 preserve every revision and compatibility profile through
    libraries/artifacts, interpreter/LLVM/mixed execution, debugger/VHPI/VCD/
    public surfaces, checkpoints, relocation, replay, caches and non-project
    phases.
15. Changes 17-19 publish standard-specific positive/negative corpora and a
    retained-log cross-engine/package closure matrix, then synchronize public
    documentation, diagnostics, inventories, counts, digests and release
    contracts.
16. Change 20 runs fresh clean-first exact-LLVM Debug/Release builds with at
    least eight workers and 120-minute command timeouts, full regressions and
    every release gate. Retain timing/RSS/swap/transcript evidence and create
    the sole Batch 166 implementation commit/push only after all local gates
    pass. Batch 166 is not a sanitizer or hosted-CI monitoring boundary.
17. Accumulate Changes 1-20 in one intentionally dirty worktree. Do not reset,
    commit or push implementation before Change 20. After that implementation
    commit, save and push the exact Batch 167 restart plan and clear context
    before Batch 167 implementation.
18. Batch 166 Change 1 is complete in the intentionally dirty worktree. The
    registered planning contract governs two 17-row active ledgers assigned
    one-to-one to Changes 2-18. The standard-mode ledger spans selection,
    source/cache identity, revision-specific lexical and semantic legality,
    predefined/protected environments, Synopsys declarations/bodies,
    artifacts, engines, public introspection, relocation, corpora and closure
    for VHDL-87/93/2000/2002. The separate package ledger covers the same
    owners for `std_logic_signed`, `std_logic_unsigned`, `std_logic_arith` and
    `std_logic_misc`, with explicit non-standard provenance and arbitrary-width
    obligations. Their SHA-256 identities are
    `2f298b8485861936db33960585ce2211bcabb501f42e97f1a0de3e777d2f39fe`
    and
    `0ff98c128af9b2229fb4ff44f0908a0a2a3d20cb6598d00ef6beb44812ec6c1b`.
    Direct and registered Debug/Release inventory checks pass; the broad static
    release slice passes 35/35. The live baseline remains 2,228 diagnostics,
    885 bounded sources and 330 authored test/control files while SPDX
    ownership advances to 1,024. Preserve Change 1 and begin Change 2 without
    reset, commit or push before Change 20.
19. Batch 166 Change 2 is complete in the same intentionally dirty worktree.
    Public `VhdlStandard` identities and canonical project helpers accept
    two-digit, full-year and `vhdl-*` forms for 1987/1993/2000/2002/2008,
    normalize every alias to the full year, keep VHDL-2008 as the default and
    reject unknown years deterministically. Manifest and direct non-project CLI
    paths share that canonicalization; CLI help lists the accepted forms. Tcl
    project dictionaries expose ordered source-profile language, canonical
    standard and library fields. Project, application and actual Tcl evidence
    passes in Debug and Release. `VHMODE-C02` and `VHSYN-C02` are preserved,
    leaving 16 active rows in each ledger; their SHA-256 identities are
    `614efdbb3d6e457f1c0bda9587d8f91a8755f899a46ae74cf46cc23e6c225689`
    and
    `1c1bd44119759ff3ade70c3fe5acc9c25f9b735a0dbd8b343aac4c3f2b716cc7`.
    Preserve Changes 1-2 and begin Change 3 without reset, commit or push
    before Change 20.
20. Batch 166 Change 3 is complete in the same intentionally dirty worktree.
    Typed frontend VHDL revision identity now flows from each canonical source
    set through parse inputs, analyzed design units, checked-source provenance,
    compilation-unit digests and mapped-library metadata while VHDL remains one
    language family. Object and mapped sources recover language/standard
    provenance, mapped units inherit their recorded VHDL revision, and
    analysis-order indexes retain revision alongside every logical entity,
    architecture, package, context and configuration identity.
    `FSIM-FE-VHORDER-011` diagnoses incompatible dependency use or reanalysis at
    the owning source span. Focused evidence covers VHDL-93 sources in a mixed
    SystemVerilog-2017 project, compatible package use, VHDL-93/VHDL-2002
    dependency mismatch and VHDL-93/VHDL-87 reanalysis. Debug and Release
    warning-clean builds and the analysis-order, diagnostic-catalog and
    inventory gates pass. `VHMODE-C03` and `VHSYN-C03` are preserved, leaving
    15 active rows in each ledger; their SHA-256 identities are
    `5112000d9afcb49529f32bd4afb3ce8c7fcff16efee7d93f751d5497ea747973`
    and
    `8b538b08e1b1eb1b39e060833c1066219f208d698d14c3d50e31e0840eb17c29`.
    Preserve Changes 1-3 and begin Change 4 without reset, commit or push
    before Change 20.
21. Batch 166 Change 4 is complete in the same intentionally dirty worktree.
    Object format 3 and design format 5 independently retain canonical VHDL
    revision and compiler-owned `fsim-synopsys-ieee-compat-v2` identity;
    Verilog/SystemVerilog retain `none`. Object compilation, design,
    project/specialization and LLVM native-object schema v116 keys all bind the
    profile. Runtime-state schema 48 preserves each lowered VHDL process's
    native profile through standalone design reload. Focused persistent-cache
    evidence proves identical-profile hits and independent revision/profile
    misses. Artifact evidence proves digest invalidation, round trips, stable
    `FSIM-ART-0001`/`FSIM-ART-0010` future-schema rejection and VHDL-2008
    restoration. Debug and Release warning-clean focused builds plus object,
    design, LLVM, application-artifact, diagnostic-catalog and inventory gates
    pass. `VHMODE-C04` and `VHSYN-C04` are preserved, leaving 14 active rows in
    each ledger; their SHA-256 identities are
    `0eda102346b24e1893015aabe7689e99ccb9484a88d3603206bc6e9cd98eceec`
    and
    `06925c1cdd7fca807b715bc5755d859754ff15714d5e6021061a05db9c750b70`.
    Preserve Changes 1-4 and begin Change 5 without reset, commit or push
    before Change 20.
22. Batch 166 Change 5 is complete in the same intentionally dirty worktree.
    The selected typed VHDL revision now reaches the lexer. Complete reserved-
    word introduction tables preserve later words as identifiers in earlier
    modes and reject them at identifier boundaries under their owning revision
    with exact-span `FSIM-VHDL-LEX-002`. Exact-span `FSIM-VHDL-LEX-001`
    diagnoses VHDL-93 extended identifiers in VHDL-87 and VHDL-2008 comments,
    delimiters and expanded bit-string forms in older modes while preserving
    legacy B/O/X and arbitrary width. Compiler-owned IEEE/Synopsys projections
    are audited under every older lexical profile. Focused frontend evidence
    covers all revision transitions, coordinates and 137-bit values; the
    Logic9 application executes a 137-bit fixture in VHDL-87/93/2000/2002 and
    rejects an explicit-width VHDL-2008 form under VHDL-2002. Debug and Release
    focused builds and frontend, application, diagnostics and inventory gates
    pass. `VHMODE-C05` and `VHSYN-C05` are preserved, leaving 13 active rows in
    each ledger. Their SHA-256 identities are
    `1991755d476f1722284b8bdd8cee3dd6ba035ee620624abac879a4afb6539a58`
    and
    `e9f5fd5877c3637749a15bf0f3f8bee385e592ac1d996aa4f7933f640b82a215`.
    Preserve Changes 1-5 and begin Change 6 without reset, commit,
    push, sanitizer or hosted-CI inspection before Change 20.
23. Batch 166 Change 6 is complete in the same intentionally dirty worktree.
    Typed revision gates cover VHDL-93 shared variables, groups, file open-kind
    clauses and alias signatures; VHDL-2000 protected types; and VHDL-2008
    interface types/subprograms/packages, package instantiations and
    unconstrained array element subtypes. VHDL-87 legacy `file ... is in/out`
    syntax normalizes to the modern semantic modes and later revisions receive
    an actionable migration. Availability uses exact `FSIM-FE-VHSTD-003`
    diagnostics while malformed empty alias signatures remain a distinct parser
    error. Positive proof preserves a 137-bit aggregate, descending 105-to-8
    and ascending 8-to-105 bounds, typed aliases, attributes, access/file
    declarations and representative declarations for every Synopsys package
    under all older modes. Full parser audits validate all intrinsic package
    projections. A VHDL-87 application executes and reloads an exact 137-bit
    output, and IEEE integration passes in Debug and Release. The required
    eight-worker clean-first Debug rebuild eliminated a stale SimIR layout;
    final Debug and Release focused builds are warning-clean. Frontend,
    advanced-type application, IEEE integration, diagnostic-catalog and
    inventory gates pass in both configurations. `VHMODE-C06` and `VHSYN-C06`
    are preserved, leaving 12 active rows in each ledger; their SHA-256
    identities are
    `6704a5d3912004498588a12eb7404de343ff01e59e17605df42b25acbfbfaf94`
    and
    `9f095548470b22953e8ddeb62914347a84efc32035a190eba243f5641f160674`.
    Preserve Changes 1-6 and begin Change 7 without reset, commit, push,
    sanitizer or hosted-CI inspection before Change 20.
24. Batch 166 Change 7 is complete in the same intentionally dirty worktree.
    Exact revision gates cover VHDL-93 `xnor` and shift/rotate operators plus
    VHDL-2008 reductions, `??`, conditional/case expressions, external names
    and suffixes on function-call results. Legal older modes retain
    qualification, named associations and universal integer resolution.
    Focused elaboration proves expected-result-type selection against an
    intentionally ambiguous package-style overload, exact descending 137-bit
    and null-array bounds and VHDL-93 operator execution. Interpreter and LLVM
    application evidence executes exact 137-bit `xnor`/shift, signed addition,
    unsigned wrap, contextual overload and zero-width null-array results;
    VHDL-2008 reduction and explicit condition results agree across engines.
    LLVM frame validation accepts the resulting zero-word register layout while
    retaining all nonempty plane checks. Debug and Release focused builds are
    warning-clean; frontend, elaboration, overload application, LLVM,
    diagnostic-catalog and inventory gates pass in both configurations.
    `VHMODE-C07` and `VHSYN-C07` are preserved, leaving 11 active rows in each
    ledger. Their SHA-256 identities are
    `9d607f7157e32b0e2f3646ae551b71e0a0a638a2cc2c045e9f7075282741e17a`
    and
    `8fe2e552fb40ea52d3c32c0b182a8a998d10e3b3f28528443fea331b13152e4a`.
    Preserve Changes 1-7 and begin Change 8 without reset, commit, push,
    sanitizer or hosted-CI inspection before Change 20.
25. Batch 166 Change 8 is complete in the same intentionally dirty worktree.
    Exact structural gates cover VHDL-93 direct entity/configuration
    instantiation, postponed concurrent statements and standalone `report`,
    plus VHDL-2008 contexts, `process(all)`, force/release, sequential
    conditional/selected assignments, matching case/select and case/
    alternative generate forms. Unavailable constructs retain parse structure
    and receive one actionable `FSIM-FE-VHSTD-003`. Older port maps retain
    static values, signal names/selections and one-argument conversion
    interpretations; composed nonstatic expressions require VHDL-2008 and an
    explicit intermediate signal otherwise. VHDL-87 component/configuration
    structure and Synopsys selected-name contexts parse under all four older
    modes. A VHDL-93 direct instance executes an exact selected 137-bit port
    actual, and the VHDL-2008 dynamic form elaborates without narrowing. Debug
    and Release focused builds are warning-clean; frontend, elaboration,
    configuration application, diagnostic-catalog and inventory gates pass in
    both configurations. `VHMODE-C08` and `VHSYN-C08` are preserved, leaving 10
    active rows in each ledger. Their SHA-256 identities are
    `6e53acbb5d71d92cc68e9598766eb5fef0aea088f7e68712b8c157f5cb7d5b78`
    and
    `da126ffa7d5dafab49e2af6692e2edd7f43ef9636e3241f4e1d756fcd2850c17`.
    Preserve Changes 1-8 and begin Change 9 without reset, commit, push,
    sanitizer or hosted-CI inspection before Change 20.
26. Batch 166 Change 9 is complete in the same intentionally dirty worktree.
    Every VHDL unit now retains an exact
    `ieee-1076-standard:<year>:fsim-v1` environment, working library, implicit
    `std`/`work` and `std.standard.all` visibility, revision-specific standard
    declarations/operators/attributes and the `fs` through `hr` time ladder.
    VHDL-93 and VHDL-2008 additions are separated exactly; standard vector,
    reduction, condition/minimum/maximum and subtype/element profiles cannot
    leak into VHDL-87/93/2000/2002. Compiler-owned IEEE projections are parsed
    in their requesting revision, later packages and mixed intrinsic revisions
    reject at stable `FSIM-FE-VHSTD-003`/`FSIM-FE-VHSTD-005` boundaries, and
    older bare VHDL-2008 string-conversion names reject with
    `FSIM-ELAB-VHSTD-001`. Focused evidence proves exact per-revision names and
    overload counts, implicit `work` component binding, 137-bit ports,
    universal arithmetic, standard attributes and the negative IEEE cases.
    Pinned clang-format 22.1.8 is applied and `git diff --check` is clean. The
    formatted Debug build completed 69 warning-clean steps; the Release build
    completed 320 warning-clean steps, both with eight workers. Frontend,
    elaboration, Logic9, numeric, IEEE integration, diagnostic-catalog and
    inventory slices pass 7/7 in both configurations. `VHMODE-C09` and
    `VHSYN-C09` are preserved, leaving nine active rows in each ledger. Their
    SHA-256 identities are
    `c1e450a8e7e1b7ae0faa96f76d0da33f8b8f6b6d4101e25fca56a2613b06dd1a`
    and
    `7015fb45b9f442cb2ba87d6c3953329d0be72e525a65209501de04592ae5f570`.
    Preserve Changes 1-9 and begin Change 10 without reset, commit, push,
    sanitizer or hosted-CI inspection before Change 20.
27. Batch 166 Change 10 is complete in the same intentionally dirty worktree.
    VHDL-1993 legacy unprotected shared scalars and packed values now own one
    statically initialized immediate shared identity across processes. Only
    those identities bypass unresolved-signal multi-driver validation; actual
    signal driver checks remain unchanged. VHDL-2000/2002/2008 shared objects
    require protected types and reject the legacy form with
    `FSIM-ELAB-VHPROTECTED-008`; invalid legacy storage uses the registered
    `FSIM-ELAB-VHPROTECTED-023`. Existing protected private storage and atomic
    non-suspending/non-reentrant method lowering now execute under VHDL-2000
    and VHDL-2002 as well as VHDL-2008. Focused elaboration proves source-
    ordered two-process VHDL-1993 updates, both older protected revisions,
    later unprotected rejection, private-state reset, and stable suspension,
    reentry and purity diagnostics. The advanced-type application proves the
    VHDL-1993/2000/2002 behavior in interpreter and LLVM with identical state,
    timing and deltas. Pinned clang-format 22.1.8 is applied and
    `git diff --check` is clean. Debug's affected 104-step elaboration and
    13-step application builds and Release's combined 118-step build are
    warning-clean with eight workers. Frontend, elaboration, advanced-type
    application, diagnostic-catalog and inventory slices pass 5/5 in both
    configurations. `VHMODE-C10` and `VHSYN-C10` are preserved, leaving eight
    active rows in each ledger. Their SHA-256 identities are
    `c1aa08f28bddf219399930c6dcb3ee3ae1bb34afc7f1ca7080b99532ca0f8170`
    and
    `ccfc8e6746bf593c43150fbe3dd4efd80cfff37baa92e8769edecd3adcc21160`.
    Preserve Changes 1-10 and begin Change 11 without reset, commit, push,
    sanitizer or hosted-CI inspection before Change 20.
28. Batch 166 Change 11 is complete in the same intentionally dirty worktree.
    Compiler-owned clean-room projections now provide historical declaration
    names and exact overload profiles for `ieee.std_logic_arith`,
    `std_logic_signed`, `std_logic_unsigned` and `std_logic_misc` under every
    VHDL revision. Each projection records an explicit package/year-qualified
    `synopsys-legacy-ieee:1990-1992:fsim-synopsys-ieee-compat-v2` identity and a
    digest-owned generated source dependency with no third-party backing path.
    `std_logic_arith` precedes its signed/unsigned dependents; the four packages
    coexist deterministically with `std_logic_1164` and VHDL-1993-or-later
    `numeric_std`. Project redeclaration and incompatible internal projection
    failures use actionable `FSIM-FE-VHSTD-004`/`006` boundaries. Frontend
    declaration evidence covers VHDL-87/93/2000/2002 and a VHDL-1993 protected
    negative; IEEE integration covers all five revisions, order, exports,
    profiles, provenance and source digests. Cache identity advances to v2 and
    its focused test was split into a dedicated source-budget-compliant file;
    the VHDL revision-profile frontend slice was likewise extracted so every
    governed file remains below 2,500 lines. Debug and Release warning-clean
    focused builds pass frontend, LLVM, artifact-profile, IEEE integration,
    diagnostics, source-line and inventory gates. `VHMODE-C11` and `VHSYN-C11`
    are preserved with seven active rows remaining. Their ledger SHA-256
    identities are
    `2095b47b281fd2e2435c8536f90d9b97b355916cc3387f521a18ab9753a40aca`
    and
    `38c48bf0597b127b671296444b9e1aadd1c94c8b684cd1495d6dd058b52a4232`.
    Preserve Changes 1-11 and
    begin Change 12 without reset, commit, push, sanitizer or hosted-CI
    inspection before Change 20.
29. Batch 166 Change 12 is complete in the same intentionally dirty worktree.
    The compiler now executes the clean-room Synopsys arithmetic, comparison,
    conversion, extension, shift and reduction families through arbitrary-width
    SimIR. Architecture-local `std_logic_signed` and `std_logic_unsigned` use
    clauses select ordinary `std_logic_vector` signedness across ascending and
    descending ranges; importing both no longer chooses silently and instead
    reports `FSIM-ELAB-VHSYN-001` on a genuinely conflicting vector operator or
    unqualified `conv_integer`. Nested conversions retain exact widths, the
    application corpus proves 137-bit results and historical vector shift
    counts, and all six reductions preserve Logic9 unknowns plus null-vector
    identities. Concrete zero-width built-in vectors now survive elaboration,
    while invariant zero-element reads are excluded from compiled process
    sensitivity. VHDL-1993 elaboration and O0/O2 interpreter, cold-LLVM and
    warm-cache application evidence pass. Debug and Release warning-clean
    frontend/elaboration/application builds and the seven focused gates pass.
    `VHMODE-C12` and `VHSYN-C12` are preserved with six active rows remaining;
    their ledger SHA-256 identities are
    `59f63e67992b380eba3872adb5173bc77520ea369925a51f9fe63731ffd92320`
    and
    `b8f3bc72890a68bad129b45a6156b43916cacb05f7e42f1dfa3fbb015fc8da54`.
    Preserve Changes 1-12 and begin Change 13 without reset, commit, push,
    sanitizer or hosted-CI inspection before Change 20.
30. Batch 166 Change 13 is complete in the same intentionally dirty worktree.
    Library format 2, object format 4 and design format 6 retain canonical
    VHDL year, predefined-environment, Synopsys package/revision and exact
    clean-room source-digest dependencies. Object/design provenance digests
    cover the records; object reload also rejects omission. Direct object,
    standalone design and mapped-library consumers regenerate current compiler
    identities and reject stale or unavailable records with
    `FSIM-ART-VHDEP-001`. Codec round trips, future-schema rejection, changed
    dependency rejection and exact 137-bit `std_logic_unsigned` arithmetic
    survive object-to-design reload. Eight focused artifact/inventory/catalog/
    source gates pass after warning-clean eight-worker Debug and Release builds.
    `VHMODE-C13` and `VHSYN-C13` are preserved with five active rows remaining;
    their ledger SHA-256 identities are
    `2e6cc664961a3f49afdb22e109adc57be6a8febfef14ae67891d55a6f83a93b6`
    and
    `66cb9bd7dae423affb85897c66592585f1aee7f643f25e6de32a93b12f6b7cf8`.
    Preserve Changes 1-13 and begin Change 14 without reset, commit, push,
    sanitizer or hosted-CI inspection before Change 20.
31. Batch 166 Change 14 is complete in the same intentionally dirty worktree.
    The mixed Logic9 application executes VHDL-87/93/2000/2002 in both
    SystemVerilog-parent/VHDL-child and VHDL-parent/SystemVerilog-child
    hierarchies under interpreter and LLVM at O0/O2. Every run preserves the
    exact Logic9-to-Logic4 projection, 137-bit ascending/descending
    `std_logic_unsigned` results, completion status, time and delta. The typed
    boundary companion retains multiple-root VHDL/SystemVerilog/SystemC,
    debugger, VCD, cache and arbitrary-width evidence; the mixed-conversion
    companion retains numeric, Boolean, bit and Logic4 conversion parity. No
    production executor correction was required. Warning-clean eight-worker
    Debug and Release application builds and the six focused execution,
    inventory, catalog and source gates pass in both configurations.
    `VHMODE-C14` and `VHSYN-C14` are preserved with four active rows remaining;
    their ledger SHA-256 identities are
    `11ac8f70c4cd047be0c10f3f5a41232a40b60b398c23443b9f41886b0ebb94bb`
    and
    `2725910c571c283e1bfb30ff297bb009712a0ff5ee45cddfd93144b01cf97f3e`.
    Preserve Changes 1-14 and begin Change 15 without reset, commit, push,
    sanitizer or hosted-CI inspection before Change 20.
32. Batch 166 Change 15 is complete in the same intentionally dirty worktree.
    Source builds retain semantic-unit-keyed VHDL year, predefined environment,
    compatibility profile and exact compiler-package dependency records.
    Debugger snapshots, execution activity, VCD comments and VHPI metadata
    expose them on real elaborated scopes while compiler packages remain absent
    from hierarchy discovery. The mixed Logic9 matrix proves stable unit/source
    identity, exact 137-bit values and force/release preservation across all
    four older revisions, both mixed-language directions, interpreter/LLVM and
    O0/O2; runtime evidence rejects partial provenance. Warning-clean
    eight-worker Debug and Release builds and all five focused gates pass.
    `VHMODE-C15` and `VHSYN-C15` are preserved with three active rows
    remaining; their ledger SHA-256 identities are
    `6bd31fc136d1bfce2648d488bcc5ce2de093b9d022af26d21effe730b1d68bd2`
    and
    `921398ba881c319fdcbe3ee3a09580b54798c5e5414f4961beaf8e086362e83d`.
    Preserve Changes 1-15 and begin Change 16 without reset, commit, push,
    sanitizer or hosted-CI inspection before Change 20.
33. Batch 166 Change 16 is complete in the same intentionally dirty worktree.
    `.fsimdesign` format 7 retains an ordered semantic-unit VHDL provenance
    index containing year, predefined environment, compatibility profile and
    exact selected package records. Its design digest covers the full index;
    load rejects missing, duplicate, out-of-range, unordered, stale or object-
    inconsistent entries before rebuilding `BuiltProject` provenance. The
    artifact-phase corpus removes the original VHDL source and producer object
    paths, then proves direct load, interpreter, cold/warm LLVM, public
    provenance comments, equal replay checkpoints, relocation and CLI VCD
    output. The non-project corpus reloads split portable VHDL objects after
    original source and producer paths disappear. Warning-clean eight-worker
    Debug and Release design/application/LLVM builds and focused artifact,
    application and LLVM gates pass in both configurations. `VHMODE-C16` and
    `VHSYN-C16` are preserved with two active rows remaining; their ledger
    SHA-256 identities are
    `b0dcbf36f31e3f2d138b18ca35abef2c76cf037a3ec6c493d2d22755a2f805bb`
    and
    `e5162897fa9aac2fea7545951d3caa81fdef629bc659ea2df1d8354135a21959`.
    Preserve Changes 1-16 and begin Change 17 without reset, commit, push,
    sanitizer or hosted-CI inspection before Change 20.
34. Batch 166 Change 17 is complete in the same intentionally dirty worktree.
    `vhdl_revision_corpus.tsv` assigns VHDL-87/93/2000/2002 legal semantic
    declarations and exact rejected-later-construct diagnostics, including
    full `FSIM-FE-VHSTD-003` messages and source coordinates. The separate
    `vhdl_synopsys_package_corpus.tsv` assigns all four compatibility packages
    positive execution, negative ambiguity/revision/artifact diagnostics,
    arbitrary-width evidence and revision-indexed provenance. The inventory
    checker freezes ordered rows, stages, codes, coordinates, evidence files
    and source anchors. Warning-clean eight-worker Debug and Release frontend/
    application builds and focused frontend, numeric, IEEE-integration and
    inventory gates pass. `VHMODE-C17` and `VHSYN-C17` are preserved with one
    active row remaining; their ledger SHA-256 identities are
    `53fbef6506d35465359f49961e7fc73b5da3ce56072b9928a281a390a6a140c5`
    and
    `098e05374b5417cf4c93f47c1bcabef4df18bd21699696c18de9905a24562cb9`.
    Preserve Changes 1-17 and begin Change 18 without reset, commit, push,
    sanitizer or hosted-CI inspection before Change 20.
35. Batch 166 Change 18 is complete in the same intentionally dirty worktree.
    `fsim.vhdl-standard-mode-closure-matrix` serially runs 15 witnesses and
    retains one verbose log plus a result-ledger row for each. The exact
    transcript contract covers VHDL-87/93/2000/2002, all four Synopsys
    packages, interpreter/LLVM O0/O2, 137-bit and ascending/descending/null
    behavior, ambiguity, mixed SystemVerilog, debugger/VHPI/VCD provenance,
    artifacts, cold/warm caches, relocation, replay, checkpoints, C/C++/Tcl
    boundaries and MSVC/Windows/tool/resource contracts. Owning executions use
    the portable 6 GiB process ceiling with delta-1000 and VCD-64 evidence;
    each stage has 1,200 seconds and the serial test has 7,200 seconds. Debug
    passes 15/15 in 68.85 seconds and Release passes 15/15 in 65.39 seconds;
    both retained directories contain a 16-line stage ledger and fifteen logs.
    `VHMODE-C18` and `VHSYN-C18` are preserved, closing both ledgers at 17
    preserved and zero active rows. Their SHA-256 identities are
    `9a97ed1964d3fd24aa4644f85dcada8bf114fc6bce42001bd092e02e9bbd8434`
    and
    `118ee444e373f2fc46624bd17fb7c9d2664c838f647f7d75bae59a10eea9f6b0`.
    Preserve Changes 1-18 and begin Change 19 without reset, commit, push,
    sanitizer or hosted-CI inspection before Change 20.
36. Batch 166 Change 19 is complete in the same intentionally dirty worktree.
    README, language support, feature-matrix guidance, architecture, cross-
    language semantics, VHPI and diagnostics now publish the five selectable
    VHDL revisions, explicitly non-standard Synopsys compatibility surface,
    exact package provenance and the serial closure contract. Memory, work,
    trace and timeout bounds are consistently physical resource ceilings, not
    VHDL legality or arbitrary-width limits. The exact diagnostic corpus moved
    to the dedicated revision-profile owner so all 887 C/C++ sources remain
    below 2,500 lines. The synchronized baseline is 2,236 diagnostics, 887
    bounded sources, 1,029 SPDX-scoped artifacts and 332 authored test/control
    files. The 1,294-row, 5,176-cell feature matrix has 617 exact evidence
    paths with canonical SHA-256
    `5f6a4d99889db39a81694c8d4ee585bdb0cdffaea577ddf275e5d3e1eca1cd4f`;
    the evidence-path identity remains
    `e4076a40c31945c3385495a1c3076b85518143712c89ea1fa3f629fffaac6120`.
    Static catalog/source/VHDL inventory, all four language/compatibility
    closure families, v1 release, installed/platform/resource and public
    documentation contracts pass. Final standard-mode/package ledger SHA-256
    identities are
    `9a97ed1964d3fd24aa4644f85dcada8bf114fc6bce42001bd092e02e9bbd8434`
    and
    `118ee444e373f2fc46624bd17fb7c9d2664c838f647f7d75bae59a10eea9f6b0`.
    Preserve Changes 1-19 and begin Change 20 without reset, commit, push,
    sanitizer or hosted-CI inspection before the fresh final local gates.
37. Batch 166 Change 20 local qualification is complete. Fresh clean-first
    exact-LLVM 22.1.8 Debug and Release eight-worker builds complete all 781
    steps warning-free in 12:03.44 and 9:48.97 at 5,015,728 and 2,253,856 KiB
    peak RSS with zero swaps. The first complete Debug regression passed
    143/144 and exposed only a stale-object-masked VHDL-2008 fixture defect:
    the analysis-order context used reserved word `shared` as an identifier.
    Rename it to legal `analysis_context` and retain diagnostic printing on an
    unexpected failure. The focused case passes three consecutive runs in
    both configurations. Complete reruns then pass Debug and Release 144/144
    in 12:12.23 and 10:24.45 wall time, with CTest totals of 732.22 and 624.44
    seconds, peak RSS of 3,786,880 and 3,791,172 KiB, and zero swaps. Their
    retained transcripts include every closure matrix and release contract.
    Final post-documentation static/release slices pass 41/41 in 21.84 and
    21.02 seconds in Debug and Release.
    Pinned clang-format 22.1.8 accepts the exact correction and `git diff
    --check` is clean. This is neither a sanitizer nor hosted-CI monitoring
    boundary. Create and push the sole accumulated Batch 166 implementation
    commit, then save and push the exact Batch 167 documentation-only restart
    checkpoint and clear context before implementation.

## Batch 165 planned restart checkpoint - 2026-08-11

1. Start in `/home/colin/projects/fsim`, read this section and the authoritative
   expanded Batch 165 allocation in `implementation_plan_v2.md`, and verify
   branch `codex/v2` is clean and synchronized at Batch 164 implementation
   `42aa87515532e19a0c750c644d08a88a3947d719` plus this documentation-only
   Batch 165 planning checkpoint.
2. This checkpoint implements the required pre-batch flow. Batch 165 is
   expanded into exactly twenty changes without broadening its locked
   SystemVerilog-2017 residual-language closure scope. No Batch 165
   implementation file has changed; clear context after pushing this plan and
   resume only from this section and the authoritative allocation.
3. Preserve Batch 164's complete IEEE 1364-2005 clause/width inventories,
   arbitrary-width Verilog frontend, elaboration, interpreter, LLVM, VPI,
   artifacts and cache behavior, scheduler/timing/hierarchy closure, public
   guide/tutorial and governed zero-gap matrix at pushed commit `42aa875`.
4. Batch 164's fresh clean-first exact-LLVM 22.1.8 Debug and Release
   eight-worker builds are warning-free. Debug completes in 12:04.92 with
   4,795,068 KiB peak RSS; Release completes in 10:18.77 with 3,266,400 KiB
   peak RSS. Both record zero swaps.
5. Complete Debug and Release regressions each pass 132/132. Debug reports
   534.37 seconds of CTest time, 8:54.38 wall time and 3,785,976 KiB peak RSS;
   Release reports 460.20 seconds of CTest time, 7:40.21 wall time and
   3,791,508 KiB peak RSS. Both record zero swaps. Final post-documentation
   release-contract slices pass 7/7 in both configurations, touched Change 20
   sources pass the WebKit gate, and repository whitespace is clean.
6. Preserve the Change 20 correction for wide Logic9 JIT frame planes: the
   public ABI guarantees eight-byte alignment, all four wide plane-two/
   plane-three loads and stores declare exactly that alignment, and the O0/O2
   regression deliberately supplies 129-bit register planes aligned to eight
   rather than sixteen bytes. Keep its adjacent width-polymorphic Logic9 masks
   and result constants.
7. The synchronized Batch 164 baseline is 2,152 diagnostics, 842 bounded C/C++
   sources, 972 SPDX-owned files and 320 authored test/control files. The
   feature matrix has 1,279 executable rows, 5,116 evidence cells and 604 exact
   paths split 265/312/27 test/production/release, with 138 runtime owners and
   36 corpus CTests.
8. Its feature and canonical evidence SHA-256 identities are
   `7f879238bfcdbc544e688c710038c97063a8464f9817c758b87c1a2f89e0472d`
   and
   `2d9a2bf8fdc360b6b484087848eeb00def2a61cff5fecfacf9400fadcec2010b`.
   Verilog gap, literal-width and closure identities are
   `338b64ba883f6243d9f799d31c22f873e871a5977bbb9fc9c45ae3b5ea738c84`,
   `4d0924571f33e54f8d77d66979e290fe223c183eb0bc963fdbffb841429a424e`
   and
   `a80eea635da93dff681c7118cd3339e7b4bb679c83a2cf7137cc3eeccef6f39e`.
9. Batch 165's cross-cutting requirement is to inventory and remove every
   arbitrary SystemVerilog bit-string, based-number, unbased and unsized
   literal-width limit. Exact source/context-determined width, signedness and
   X/Z planes must survive parsing, constant folding, elaboration, interpreter,
   LLVM, public boundaries, artifacts, caches, relocation and mixed-language
   conversion. Retain and distinctly diagnose only true host-addressability or
   governed resource ceilings.
10. Changes 1-4 freeze the IEEE 1800-2017 residual and width-limit inventories,
    then close lexical/preprocessing/declaration/namespace and type-system
    gaps, including nettype, alias, let, packages, arbitrary-width enums,
    packed aggregates and virtual interfaces.
11. Changes 5-8 close expression sizing/casts/patterns/streaming, containers and
    files, classes/interfaces/programs/checkers/callables, constraints and
    deterministic randomization without host-word narrowing.
12. Changes 9-12 close hierarchy/bind/configuration, process/assignment/
    scheduler semantics, timing/clocking/program regions, assertions and
    functional coverage with exact time/delta/region and driver identity.
13. Changes 13-16 close standard services and introspection, DPI/VPI, UVM, and
    multiple-root/mixed-language/debug/VCD/public integration. Preserve the
    locked later ownership of FST, SDF and Accellera SystemC/TLM/SCV work.
14. Changes 17-19 preserve all new behavior through artifacts, checkpoints,
    libraries, caches, relocation and replay; run the governed zero-gap
    cross-engine/platform matrix; and synchronize public documentation,
    diagnostics, matrices, counts, digests, contracts and this handoff.
15. Change 20 runs fresh clean-first exact-LLVM Debug/Release builds with at
    least eight workers and 120-minute command timeouts, full regressions and
    every release gate. Retain timing/RSS/swap/transcript evidence and create
    the sole Batch 165 implementation commit/push only after all local gates
    are clean. Batch 165 is not a sanitizer or hosted-CI monitoring boundary.
16. Accumulate Changes 1-20 in one intentionally dirty worktree. Do not reset,
    commit or push implementation before Change 20. After that implementation
    commit, save and push the exact Batch 166 restart plan and clear context
    before Batch 166 implementation. The approved Batch 166 scope includes the
    legacy non-standard Synopsys `ieee.std_logic_signed`,
    `ieee.std_logic_unsigned`, `ieee.std_logic_arith` and
    `ieee.std_logic_misc` packages without arbitrary numeric width limits.
17. Change 1 is complete in the intentionally dirty Batch 165 worktree. The
    new authoritative IEEE 1800-2017 inventory covers every normative clause
    3-40 with 15 reviewed supported families, 15 active rows assigned
    one-to-one to Changes 2-16, and five explicit SDF, FST, Accellera
    SystemC/TLM/SCV, encrypted-envelope and deprecated-TF/ACC deferrals. Every
    row owns exact implementation, positive, negative, execution, diagnostic
    and resource paths. The registered contract rejects missing, duplicate,
    misplaced or drifted ownership and failure escapes.
18. The separate 25-row bit-string/host-assumption ledger freezes 16 preserved
    paths, five active obligations and four physical boundaries. Active owners
    are Change 6 for above-64-bit integral associative indices, Change 12 for
    wide coverage literals/bins, Change 14 for arbitrary-width VPI enums, and
    Change 16 for wide exact-Logic9 compiled execution and debugger locals.
    Governed constant, DPI and UVM budgets plus scalar host formats remain
    explicitly physical rather than SystemVerilog legality limits.
19. Direct validation passes, the exact-LLVM Debug tree regenerates with an
    eight-worker no-op warning-free build, and
    `fsim.systemverilog-gap-inventory` passes 1/1 in 0.03 seconds. Whitespace is
    clean. Clause and width SHA-256 identities are
    `1778ecdf10191a65e4400f6b0d353c301ba74f0e41bc68561553c3e7a4ff64f5`
    and
    `e7d242e15706352d4de9844eb203c59f6d5b5cf88714a34e9bc274df75674efe`.
20. Batch 165 Change 2 is complete in the same intentionally dirty worktree.
    The normative directive, keyword, attribute and provenance surfaces were
    already complete; the six legacy Annex D delay-mode directives are
    informative rather than required SystemVerilog syntax. Decimal based
    numbers now preserve X versus Z/? fills at 257 bits, reject mixed unknown
    decimal digits and retain exact signed/unsized identity. Unbased-unsized
    syntax now admits only `'0`, `'1`, `'x` and `'z`. SystemVerilog strings add
    `\v`, `\f`, `\a` and exact two-digit `\xhh` escapes while malformed forms
    and Verilog-2005 use retain stable `FSIM-SV-SEM-040` diagnostics.
21. Focused frontend and elaboration tests pass 2/2 after an eight-worker
    build. The expression application passes interpreter versus compiled O0/O2
    parity for exact 257-bit decimal X/Z/? and context-sized unbased one/X
    values. The direct and registered inventory contract passes with 16
    supported, 14 active and 5 deferred rows; the combined focused CTest passes
    2/2. Clause and unchanged width SHA-256 identities are
    `a6a83611f3235e915303a17f425acba0052927f33bf71242b36bc57ac6db09cd`
    and
    `e7d242e15706352d4de9844eb203c59f6d5b5cf88714a34e9bc274df75674efe`.
22. Preserve Changes 1-2 and begin Batch 165 Change 3's declaration and
    namespace closure. Do not commit, push, run sanitizer or inspect hosted CI
    before Change 20.
23. Batch 165 Change 3 is complete in the same intentionally dirty worktree.
    Frontend, semantic HIR and portable artifacts retain nettype resolver,
    alias and let declarations; lets expand in local, package-qualified,
    imported and generated scopes with positional, named and default arguments
    under a governed recursion/work budget. Executable user nettypes validate
    and run the retained deterministic first-driver resolver.
24. SystemVerilog aliases accept locally static packed-net identifiers,
    constant selections and concatenations. Exact compatible whole nets share
    one runtime signal; partial/range-distinct/concatenated terminals lower to
    compact bidirectional switch regions with exact source/target offsets and
    width. Runtime state schema 26 and native cache schema 91 preserve that
    topology. Variable, dynamic/out-of-range, width-mismatched and incompatible
    terminals retain stable diagnostics. The same selected-region machinery
    executes ordinary `tran` part selects.
25. After the eight-worker rebuild, frontend, elaboration, runtime,
    library-artifact, SystemVerilog-HIR application, diagnostic-catalog and
    inventory gates pass 7/7, including interpreter/O0/O2 and artifact
    round-trip evidence. The inventory reports 17 supported, 13 active and 5
    deferred rows. Clause and unchanged width SHA-256 identities are
    `ff38eceef81ff64b53b0df2bcb752ae493440a6e693b42ed59f89e5108ad59fe`
    and
    `e7d242e15706352d4de9844eb203c59f6d5b5cf88714a34e9bc274df75674efe`.
    The parser split leaves 1,585 and 1,071-line owners; the source-budget gate
    passes across 846 authored sources, and formatting/whitespace checks are
    clean. Preserve Changes 1-3 and begin Change 4 without commit, push,
    sanitizer or hosted-CI inspection before Change 20.
26. Batch 165 Change 4 is complete in the same intentionally dirty worktree.
    `bit`, `byte`, `shortint`, `int`, `longint`, real-family and handle values
    use exact two-state defaults; `logic`, `reg`, `integer` and `time` use exact
    four-state defaults. Arbitrary-width enums and packed aggregates retain
    nominal compatibility and exact default planes. Ordinary packed unions
    reject unequal concrete widths in the frontend and unequal widths exposed
    by named-type or parameter resolution during elaboration. Packed and
    unpacked declarations no longer have an arbitrary four-dimension cap.
27. The SystemVerilog `type` operator supports exact static equality and
    inequality over scalar, vector, enum, aggregate and multidimensional array
    profiles with targeted arity and contextual-use diagnostics. Type
    parameters no longer reject widths above 64 bits. Their versioned
    `sv-type-v3` identity includes scalar family, enum values, packed dimension
    shape, recursive container elements, virtual-interface identity and class
    actuals, preventing semantic cache collisions. A 137-bit actual passes
    interpreter and compiled O0/O2 cold/warm execution and cache invalidation.
    Unknown 64-bit `time` defaults preserve exact X/Z planes through VPI while
    known time values retain the scalar tick representation.
28. Focused frontend, elaboration, runtime, SystemVerilog-HIR, type-parameter,
    interface, container, capacity and aggregate application evidence passes
    9/9 after eight-worker builds. The inventory advances to 18 supported, 12
    active and 5 deferred rows with clause SHA-256
    `8f6186d96036140228abfed79282b707193692e2697f5d7c3e115570a2128ffc`;
    the unchanged width identity remains
    `e7d242e15706352d4de9844eb203c59f6d5b5cf88714a34e9bc274df75674efe`.
    Preserve Changes 1-4 and begin Change 5 without commit, push, sanitizer or
    hosted-CI inspection before Change 20.
29. Batch 165 Change 5 is complete in the same intentionally dirty worktree.
    Arbitrary-width self/context sizing, signedness, casts, assignment
    patterns, concatenation, replication, streaming and static/dynamic
    selections retain exact value planes. Explicit `ConvertToTwoState` SimIR
    semantics are shared by the interpreter and LLVM; constant and runtime
    logical/conditional expressions short-circuit lazily without losing the
    common result profile.
30. `inside` and `case inside` use common-profile comparison. `case matches`
    executes wildcard, scalar-binding, guarded, tagged-union and packed-
    structure patterns with deterministic binding scope; mixed keyed/
    positional patterns and incomplete positional structures are rejected.
    Known 137-bit `dist` weights reach the governed sampler when their numeric
    magnitude is representable. Runtime-base selections and packed streaming
    no longer carry the retired 64-bit cap, and native schema 92 separates the
    new lowering/cache identity.
31. The eight-worker focused build is warning-free. Frontend, elaboration,
    runtime, LLVM, SystemVerilog-HIR, expression application, diagnostic,
    inventory and source-budget gates pass 9/9. The diagnostic catalog covers
    2,175 codes. Assignment/control lowering was coherently split to 1,848 and
    1,785 lines, and all 846 authored sources satisfy the hard budget.
32. The inventory advances to 19 supported, 11 active and 5 deferred rows.
    Clause and unchanged width SHA-256 identities are
    `4d58d35a49d6e55f5192e074603270228246d7ace06fc31389b07c13667b92da`
    and
    `e7d242e15706352d4de9844eb203c59f6d5b5cf88714a34e9bc274df75674efe`.
    Preserve Changes 1-5 and begin Change 6 without commit, push, sanitizer or
    hosted-CI inspection before Change 20.
33. Batch 165 Change 6 is complete in the same intentionally dirty worktree.
    Integral associative-array indices no longer narrow to 64 bits:
    elaboration accepts exact executable widths through the explicit `uint32_t`
    metadata boundary, assignment-pattern duplicate checks retain every word,
    runtime validation checks every X/Z plane, and signed/unsigned ordering
    compares all limbs. Packed scalar, enum, struct and union indices are
    integral; real-family, handle, unpacked and unresolved index profiles
    retain `FSIM-ELAB-SVCONTAINER-013`.
34. The container application executes 137-bit signed scalar keyed patterns,
    lookup, mutation, delete, `exists` and ordered traversal plus a 137-bit
    packed-struct key under interpreter and LLVM O0/O2 cold/warm execution.
    The runtime unit independently proves above-host-word ordering and rejects
    an unknown bit above the low limb. Existing recursive strings, fixed and
    dynamic arrays, queues, slices, iteration, files, memories and aggregate
    containers retain governed owning-storage and input-work diagnostics.
35. Eight-worker focused builds are warning-free. Frontend, elaboration,
    container elaboration, runtime, mutable-string, container,
    governed-capacity, file/memory and multidimensional-aggregate applications
    plus diagnostic, inventory and source-budget contracts pass 12/12 in
    118.87 seconds. The catalog remains 2,175 codes and all 846 authored sources
    satisfy the hard budget. No state or native schema bump is required because
    the owning representation and cache identity were already word-vectorized
    and formerly rejected wide designs could not have emitted valid artifacts.
36. The inventory advances to 20 supported, 10 active and 5 deferred rows at
    SHA-256
    `92ae4cdf43fa3ca8ba6183d76fbc02e2a9c2f6fd2df7f40053e33559d48fee52`.
    The width ledger advances to 17 preserved, 4 active and 4 physical rows at
    SHA-256
    `52a85d7c02aa6ef5b47bbebc54edd88b01f8b48862f1af2ed628b4c00daeac25`.
    Preserve Changes 1-6 and begin Change 7 without commit, push, sanitizer or
    hosted-CI inspection before Change 20.
37. Batch 165 Change 7 is complete in the same intentionally dirty worktree.
    The reviewed class, interface, program, checker, inheritance,
    virtual/pure, constructor, argument, recursion, nested-scope, modport and
    handle surfaces were already represented and exercised. The residual live
    rejection was exact mutable-string `output`/`inout` copy-out from instance
    and static class functions.
38. Class lowering now sends string registers through the common ordered
    callable copy-out service. Source-method execution binds string formals,
    automatic locals and explicit static-lifetime locals as byte strings rather
    than placeholder packed values. The application fixture proves exact
    instance and object-free static output/inout values, automatic
    reinitialization and explicit static persistence across interpreter,
    compiled and debug
    engines. Packed arguments, virtual dispatch, task suspension, modport
    callables and virtual-interface/class identities remain unchanged.
39. No SimIR, runtime-state or native-object schema bump is required because
    class operations already retained actual-kind metadata and mutable string
    vectors, and the previously rejected lowering could not emit a valid
    artifact. Eight-worker owning builds are warning-free. Frontend,
    diagnostics, source-budget, inventory, elaboration, runtime, class,
    interface, callable-closure, suspending-task and mutable-string gates pass
    11/11 in 20.07 seconds. The catalog remains 2,175 codes and all 846 authored
    sources satisfy the budget.
40. The inventory advances to 21 supported, 9 active and 5 deferred rows at
    SHA-256
    `0343b2cace6bf829855aa6ffcc4613bbda453c54c59bac0d97d93f73d7ebf1eb`.
    The unchanged width ledger remains 17 preserved, 4 active and 4 physical at
    SHA-256
    `52a85d7c02aa6ef5b47bbebc54edd88b01f8b48862f1af2ed628b4c00daeac25`.
    Preserve Changes 1-7 and begin Change 8 without commit, push, sanitizer or
    hosted-CI inspection before Change 20.
41. Batch 165 Change 8 is complete in the same intentionally dirty worktree.
    Source `randomize with { ... }` blocks retain structured solve-before,
    implication, conditional, distribution, `inside`, soft and arbitrary-width
    expression templates. Those templates lower into portable SimIR, bind to
    exact class or scope solver variables at execution, survive runtime-state
    schema 27 and owning design schema 21, and participate recursively in
    native-cache schema 93. The parser preserves empty blocks explicitly and
    recognizes qualified `std::randomize` calls.
42. Existing class constraints retain exact `rand`/`randc` revisions and cycle
    state, inheritance/override composition, materialized object/container
    graph lowering, transactional callbacks and deterministic per-root/object/
    call seed streams. Packed domains and inline constants have no host-word
    language cap. The SimIR metadata width and caller-governed finite-domain,
    search, elapsed-work, heap and cycle budgets remain separately diagnosed
    physical/resource boundaries.
43. Frontend, runtime and application evidence proves retained inline syntax,
    exact 137-bit template binding, solve ordering, distribution, implication,
    `inside`, soft selection, object callbacks and `randc` state. Interpreter
    and LLVM O0/O2 replay agree exactly, changed seeds diverge, standalone and
    relocated artifacts retain the inline class call, and the LLVM cache suite
    distinguishes two templates that differ only in a high bit of a 137-bit
    constant. The eight-worker owning rebuild is warning-free. The complete
    Change 8 gate passes 11/11 in 42.84 seconds; formatting and whitespace are
    clean. The catalog has 2,177 codes and all 846 authored sources pass policy.
44. The inventory advances to 22 supported, 8 active and 5 deferred rows at
    SHA-256
    `19af6260db7e11d148fc685df3f5a0ed9b8d137de0e4d3a7422f9d1c8c64042a`.
    The unchanged width ledger remains 17 preserved, 4 active and 4 physical at
    SHA-256
    `52a85d7c02aa6ef5b47bbebc54edd88b01f8b48862f1af2ed628b4c00daeac25`.
    Preserve Changes 1-8 and begin Change 9 without commit, push, sanitizer or
    hosted-CI inspection before Change 20.
45. Batch 165 Change 9 is complete in the same intentionally dirty worktree.
    The frontend now retains extern module/interface/program declarations,
    compilation-unit and unit-local bind directives with indexed targets, and
    SystemVerilog configuration design/default/instance/cell/use/liblist and
    nested-configuration clauses. Configuration rules apply instance before
    cell selection, select exact logical-library units, and preserve the
    configured root alias while matching rules and binds against the source
    design hierarchy.
46. Elaboration validates extern headers through canonical `sv-type-v3`
    identity, injects every bound instance without changing its target unit,
    resolves the injected unit in the bind declaration's logical library, and
    diagnoses collisions or unmatched directives. Ordered configuration
    selection, generated indexed paths and `defparam` overrides feed stable
    `sv-config-v1` specialization identities. Existing package, interface,
    program and checker behavior remains on the shared hierarchy path.
47. Semantic unit identity and portable owning-unit schema 22 retain extern,
    bind and configuration metadata. Deterministic library round trips validate
    all new enums and structural invariants. Focused application evidence proves
    four configured leaf instances across `fast` and `slow` libraries, all-leaf
    and selected generated binds, 137-bit parameters and exact interpreter
    versus LLVM O0/O2 values and timing.
48. The eight-worker owning build is warning-free. Frontend, library artifact,
    diagnostics, source-budget, inventory, resource, elaboration and hierarchy
    application gates pass 8/8 in 1.84 seconds; new files pass the WebKit check
    and repository whitespace is clean. The catalog contains 2,199 codes and
    all 851 authored sources satisfy policy. The inventory advances to 23
    supported, 7 active and 5 deferred rows at SHA-256
    `ecf6ab5b320cb98a831e910e522b53153f47f599969da4598f3a167973ef75da`.
    The unchanged width ledger remains 17 preserved, 4 active and 4 physical at
    `52a85d7c02aa6ef5b47bbebc54edd88b01f8b48862f1af2ed628b4c00daeac25`.
    Preserve Changes 1-9 and begin Change 10 without commit, push, sanitizer or
    hosted-CI inspection before Change 20.
49. Batch 165 Change 10 is complete in the same intentionally dirty worktree.
    Process handles execute generation-safe `suspend`, `resume`,
    `get_randstate`, `set_randstate` and `srandom` operations through the common
    interpreter/LLVM boundary. `wait_order` preserves exact source ordering,
    repeated events and success/failure selection. Existing fork/join,
    disable, wait, assignments, force/release, drivers and scheduler regions
    retain deterministic time/delta behavior.
50. Event variables now carry synchronization-object identities rather than
    copied toggle values. Blocking assignment and declaration initialization
    alias identities; `null` is non-triggerable; armed waits capture the event
    object; delayed notification retains its scheduled object across later
    rebinding; aliases wake together; and `.triggered` remains true across all
    deltas at the triggering simulation time. Runtime schema 28 and native
    object schema 94 retain the new metadata and operations without a language
    width cap.
51. Focused application evidence covers declaration and procedural aliases,
    null, same-time/expired `.triggered`, repeated and failing `wait_order`,
    process control, fork/disable and cold/warm interpreter versus LLVM O0/O2
    execution. Elaboration rejects a non-event alias source with
    `FSIM-ELAB-SVEVENT-009` and nonblocking/timed aliasing with
    `FSIM-ELAB-SVEVENT-010`.
52. The warning-free eight-worker owning rebuild and composed frontend,
    elaboration, runtime, LLVM, named-event, fork, diagnostic, source,
    inventory and resource gate pass 10/10 in 39.97 seconds. The catalog covers
    2,211 production codes. All 852 authored sources satisfy the hard budget
    after splitting `simir_execution.cpp` to 1,994 lines and its included
    boundary implementation to 552 lines. WebKit changed-range formatting and
    repository whitespace are clean.
53. The SystemVerilog inventory advances to 24 supported, 6 active and 5
    deferred rows at SHA-256
    `a086f5fd7d8ee13dcfe8432966eaf11679325cf1583f0797c94ac97a98986a1c`.
    The unchanged width ledger remains 17 preserved, 4 active and 4 physical
    at SHA-256
    `52a85d7c02aa6ef5b47bbebc54edd88b01f8b48862f1af2ed628b4c00daeac25`.
    Preserve Changes 1-10 and begin Change 11 without commit, push, sanitizer
    or hosted-CI inspection before Change 20.
54. Batch 165 Change 11 is complete in the same intentionally dirty worktree.
    The scheduler now orders active, inactive, update, observed, reactive,
    re-inactive, re-update and postponed regions. Clocking input samplers carry
    explicit observed-process ownership, so they see ordinary nonblocking
    updates before reactive program consumers. A program `#0` resumes in
    re-inactive in the same time slot and program nonblocking commits route
    through re-update. Invalid multiple-region process ownership rejects.
55. Runtime-state schema 29, DesignIR schema 3 and native-object schema 95
    retain observed ownership and invalidate stale artifacts/cache objects.
    Append-only public C phase values expose observed, re-inactive and re-update
    while preserving every earlier phase ordinal. VHPI update callbacks cover
    both update regions; existing synchronization and read-only callbacks keep
    their reactive and postponed identities.
56. Focused application evidence combines a default clocking block,
    `##1`/`##2`, an input sample after NBA, output skew, reactive program
    execution, same-slot `#0`, `$period`/`$width` timing checks, runtime artifact
    round trip and exact interpreter versus cold/warm LLVM O0/O2 results. The
    warning-free eight-worker owning rebuild and composed frontend,
    elaboration, LLVM, runtime, specify, timing, API/C-header, diagnostic,
    source, inventory and resource gate pass 12/12 in 37.11 seconds.
57. The catalog remains 2,211 production codes and all 852 authored sources
    satisfy policy. The SystemVerilog inventory advances to 25 supported, 5
    active and 5 deferred rows at SHA-256
    `d2738ab7369ef64cef6cd7c8f6237e10b48d8629fad9a8e08a05ca331420f522`.
    The unchanged width ledger remains 17 preserved, 4 active and 4 physical
    at SHA-256
    `52a85d7c02aa6ef5b47bbebc54edd88b01f8b48862f1af2ed628b4c00daeac25`.
    Preserve Changes 1-11 and begin Change 12 without commit, push, sanitizer
    or hosted-CI inspection before Change 20.
58. Batch 165 Change 12 is complete in the same intentionally dirty worktree.
    Executable named property and sequence value profiles accept positional,
    named and default actuals. Integral property and sequence locals lower to
    exact process variables whose initializers execute independently in every
    forked attempt. Simple and fixed/ranged two-element `first_match` sequences
    execute ordered blocking or compound assignments, increment/decrement and
    subroutine calls only at the completed match point. Focused interpreter and
    compiled evidence covers repeated 137-bit initialization, attempt isolation,
    match-item ordering and exact assertion coverage/event equality.
59. Coverpoint `with` filters retain exact token/span ownership, filter values
    before array-bin distribution and evaluate arbitrary-width signed operands.
    Cross `with` plus optional `matches(n)` or `matches($)` counts exact tuples
    across arbitrary-width exact, range, wildcard, automatic and governed
    default-bin complement domains. Excess candidate-domain growth rejects with
    `FSIM-SV-COV-005` before observable mutation; the sampler rolls back all
    coverpoint, transition, previous-sample, cross and diagnostic state. The
    obsolete `FSIM-SV-SEM-220` unsupported boundary is removed.
60. Owning-unit schema 24 and standalone coverage-state schema 4 preserve the
    new filter metadata. Exact `.fsimobj` and `.fsimdesign` round trips retain
    coverpoint `with` tokens and wide X/Z planes, and future-schema corruption
    continues to reject directly; no migration or compatibility path is
    retained. The split artifact-codec rebuild completes every family and
    operation translation unit promptly without the prior monolithic compile
    stall.
61. The warning-free eight-worker owning rebuild and composed SystemVerilog HIR,
    frontend, library/object/design artifact, elaboration, runtime, assertion,
    diagnostic, source, inventory and resource gate pass 11/11 in 30.03
    seconds. The catalog covers 2,209 production codes and all 875 authored
    sources satisfy policy. The inventory advances to 26 supported, 4 active
    and 5 deferred rows; the width ledger advances to 18 preserved, 3 active
    and 4 physical rows. Their SHA-256 identities are
    `0b5e39272948d01499d4d965336bb98e222baec6bd340757ba69d50f7c4906c7`
    and
    `9dbead239396bba2bc98adc97f0e5a0c156cddc87de8e7a08a437c222ad25e44`.
    Preserve Changes 1-12 and begin Change 13 without commit, push, sanitizer
    or hosted-CI inspection before Change 20.
62. Batch 165 Change 13 is in progress in the same intentionally dirty
    worktree. The completed first service slices implement exact
    `$test$plusargs`, `$value$plusargs`, `$srandom`, scalar math/conversion,
    radix terminal/file/string formatting and `$typename` behavior. The
    `$timeformat` slice now owns one simulation-global profile shared by
    interpreter and compiled terminal, monitor, string and file formatting.
    It preserves exact non-power-of-ten project resolution scaling, standard
    bare-`%t`, `%0t` and explicit-width behavior, arbitrary governed decimal
    precision, suffixes and the default 20-character field.
63. Focused interpreter, LLVM O0/O2 cold/warm, runtime serialization,
    non-power-of-ten resolution and file round-trip evidence passes. The
    adjacent display and SystemVerilog file applications plus the diagnostic
    catalog pass, touched C++ is WebKit-clean and repository whitespace is
    clean. The latest Ninja records show the split artifact coordinator at
    82.488 seconds, its two heaviest operation families at 74.988 and 61.819
    seconds, and every other codec family at 27.096 seconds or less; no codec
    translation unit retains the former five-minute compile stall. Continue
    Change 13's remaining standard services without marking its inventory row
    supported, and do not commit, push, run sanitizer or inspect hosted CI
    before Change 20.
64. The next Change 13 service slice implements `$printtimescale` through one
    portable SimIR marker and the application-owned DesignIR/HIR hierarchy
    service. Calls report the retained unit and precision for the executing
    instance, a relative child selection or a `$root` path without embedding
    runtime pointers in SimIR. Current and selected 1ns/1ps versus 10ns/100ps
    instances pass interpreter and LLVM O0/O2 cold/warm differentials; invalid
    arity and non-hierarchical operands retain `FSIM-ELAB-SVTIME-002`.
65. Change 13 growth exposed `llvm_jit_cache_key.cpp` at 2,522 lines. Its exact
    operation visitor is mechanically extracted into a 1,920-line WebKit-clean
    `.tpp`, leaving the owning cache-identity source at 604 lines without
    changing the compiled translation unit or cache bytes. The source policy
    passes across 877 authored files; the cache-key object rebuild completes
    in approximately 23 seconds, and `fsim.llvm`, `fsim.cache`, the time,
    display and file applications plus the diagnostic catalog pass. Continue
    the remaining Change 13 services without commit, push, sanitizer or hosted
    CI inspection before Change 20.
66. The next bounded Change 13 slice wires the existing checked scalar-time
    runtime service into `$time`, `$stime` and `$realtime`. Bare and
    empty-parentheses forms retain the executing specialization's unit and
    precision in portable SimIR; interpreter and LLVM execution combine them
    with the common project tick resolution. Nested 1ns/1ps and 10ns/100ps
    instances now prove distinct integral and realtime results at the same
    scheduler tick, with artifact and cold/warm compiled parity. Runtime-state
    schema 36 and native-object schema v102 intentionally replace their prior
    versions; no migration or old-schema compatibility is retained. The
    warning-free eight-worker application rebuild passes; focused time,
    display, runtime, LLVM, cache, diagnostic-catalog and source-budget gates
    pass. The latest header-driven rebuild records the extant artifact-codec
    coordinator and heaviest operation family at about 60.5 and 60.7 seconds
    under eight-worker contention, with no return of the former five-minute
    monolithic compile.
67. The next Change 13 slice implements zero-argument `$get_coverage()` as a
    portable value-producing SimIR boundary. The application-owned service
    computes the goal-adjusted overall percentage from live persisted
    covergroup declarations and instances, weighting each materialized type by
    `type_option.weight` and excluding empty or zero-weight types. Exact 0,
    43.75, 62.5 and 100 percent observations pass source execution through the
    interpreter and LLVM O0/O2 cold/warm caches; direct frontend evidence proves
    type weighting, and the runtime boundary executes exactly once. Runtime
    state schema 37 and native-object schema v103 intentionally replace their
    predecessors without migration support. Focused coverage, frontend and
    runtime gates pass after warning-clean eight-worker builds. Preserve the
    accumulated Change 13 work and continue its remaining standard services;
    do not mark `SV17-C13-GAP` supported, commit, push, run sanitizer or inspect
    hosted CI before Change 20.
68. The next bounded Change 13 slice implements `$fstrobe*` and `$fmonitor*`
    by extending the existing postponed monitor operation with an optional
    file-handle register. File strobes are one-shot; file monitors replace the
    persistent global registration and share `$monitoron`/`$monitoroff`
    control. The descriptor is captured when the task executes while supported
    direct packed signals are sampled after same-slot NBA publication. The
    common manifest-confined file service performs the write in both
    interpreter and compiled execution. The file application matrix proves
    all four radix profiles, one-shot and persistent final-value sampling,
    stop/resume, runtime-state round trip, an exact compound-expression
    diagnostic, and interpreter/LLVM O0/O2 cold/warm parity. Runtime-state
    schema 38 and native-object schema v104 intentionally replace their
    predecessors; no migration or old-schema compatibility is retained.
    Preserve the accumulated Change 13 work and continue its remaining standard
    services without marking `SV17-C13-GAP` supported, committing, pushing,
    running sanitizer, or inspecting hosted CI before Change 20.
69. The next Change 13 coverage-service slice adds zero-argument
    `$get_inst_coverage()` beside `$get_coverage()`. One explicit portable
    query kind selects either the existing type-weighted aggregate or the new
    aggregate in which every materialized covergroup instance contributes with
    its effective instance `option.weight`; empty and zero-weight contributors
    remain excluded. Direct frontend evidence distinguishes 40 percent
    instance weighting from 50 percent type weighting, while the source fixture
    distinguishes 37.5 from 43.75 percent and proves exact 0, partial, advanced
    and complete values. Runtime-state schema 39 and native-object schema v105
    intentionally replace their predecessors without migration support. The
    warning-clean 302-step eight-worker header rebuild and focused frontend,
    runtime, LLVM, cache, coverage-application, diagnostic-catalog and source-
    budget gates pass. Preserve the accumulated Change 13 work and continue
    its remaining standard services without marking `SV17-C13-GAP` supported,
    committing, pushing, running sanitizer, or inspecting hosted CI before
    Change 20.
70. The next Change 13 random-service slice implements the complete legacy
    `$dist_uniform`, `$dist_normal`, `$dist_exponential`, `$dist_poisson`,
    `$dist_chi_square`, `$dist_t`, and `$dist_erlang` family. One typed portable
    operation carries the signed 32-bit result, writable inout seed and one or
    two integral parameters. Interpreter execution and the LLVM host boundary
    invoke the same standard reference generator and distribution transforms;
    seed arithmetic uses defined 32-bit unsigned wraparound. The random
    application proves exact values and every intermediate seed for all seven
    families in interpreter and LLVM O0/O2 execution, plus invalid-domain and
    non-writable-seed behavior. The LLVM cache matrix separately proves kind
    and operand-register identity. Runtime-state schema 40 and native-object
    schema v106 intentionally replace their predecessors without migration
    support. The warning-clean eight-worker owning build and focused frontend,
    runtime, LLVM and random-application gates pass. Preserve the accumulated
    Change 13 work and continue its remaining standard services without
    marking `SV17-C13-GAP` supported, committing, pushing, running sanitizer,
    or inspecting hosted CI before Change 20.
71. The next Change 13 miscellaneous-service slice implements IEEE 1800-2017
    `$system` with the exact zero-or-one string profile, task or function use,
    C `system(NULL)` no-argument query and raw signed 32-bit C return value.
    One portable string operation retains optional command and destination
    identities; interpreter and LLVM execution use the same serialized
    simulation hook. The application installs C `system()` by default while
    exposing an override for embedding policy and deterministic tests. Direct
    runtime evidence covers command bytes, NULL, discarded task results, raw
    result bits and missing service behavior. The application evidence covers
    runtime-state round trip plus interpreter and LLVM O0/O2 parity, and the
    LLVM cache matrix distinguishes command/destination presence and IDs.
    Runtime-state schema 42 and native-object schema v108 intentionally replace
    their predecessors without migration support. Preserve the accumulated
    Change 13 work and continue its remaining standard services without marking
    `SV17-C13-GAP` supported, committing, pushing, running sanitizer, or
    inspecting hosted CI before Change 20.
72. The following Change 13 slice implements IEEE 1800-2017 20.16
    `$q_initialize`, `$q_add`, `$q_remove`, `$q_full`, and `$q_exam` through
    one typed SimIR operation. Simulation-owned FIFO/LIFO objects enforce the
    standard capacity, undefined/empty/duplicate/type/length/allocation status
    codes and deterministic current-length, mean-interarrival,
    maximum-occupancy, shortest-wait, live-longest-wait, and average-wait
    statistics in simulation ticks. Interpreter and LLVM share the same kernel
    evaluator; the full optional operand shape participates in validation,
    runtime artifacts, and native-cache identity. Runtime-state schema 42 and
    native-object schema v108 replace their predecessors without migration.
    Focused runtime and application evidence cover FIFO/LIFO ordering, every
    ordinary status, statistics, artifact round trip, and interpreter/LLVM
    O0/O2 parity. Continue the remaining Change 13 standard services without
    marking `SV17-C13-GAP` supported, committing, pushing, running sanitizer,
    or inspecting hosted CI before Change 20.
73. The next Change 13 slice implements every IEEE 1800-2017 20.17 PLA task:
    synchronous/asynchronous AND, NAND, OR and NOR over array and plane
    personality formats. One arbitrary-width `PlaEvaluate` operation reads the
    live fixed memory; asynchronous calls spawn detached re-evaluation loops
    whose `WaitPla` boundary observes both packed input signals and personality
    memory writes. Runtime-state schema 43 and native-object schema v109 replace
    their predecessors without migration. Focused evidence covers all sixteen
    task forms, input and personality changes, 137-bit input and output terms,
    artifact round trip, and interpreter/LLVM O0/O2 parity. Continue the
    remaining Change 13 standard services without marking `SV17-C13-GAP`
    supported, committing, pushing, running sanitizer, or inspecting hosted CI
    before Change 20.
74. The next Change 13 slice implements the IEEE four-state VCD controls
    `$dumpfile`, `$dumpvars`, `$dumpoff`, `$dumpon`, `$dumpall`, `$dumplimit`,
    and `$dumpflush`, plus the extended `$dumpports`, `$dumpportsoff`,
    `$dumpportson`, `$dumpportsall`, `$dumpportslimit`, and `$dumpportsflush`
    family. A typed SimIR/application boundary resolves module, variable, and
    direct-port selections against DesignIR, begins dumping in the postponed
    phase, emits standard checkpoints, confines filenames to the project root,
    and owns independent extended files through final-time `$vcdclose`. Exact
    137-bit values, port indices, direction/strength records, nested-scope
    exclusion, default/null-scope behavior, limits, runtime artifacts, and
    interpreter/LLVM O0/O2 parity have focused application evidence.
    Runtime-state schema 44 and native-object schema v110 replace their
    predecessors without migration.
    Continue the remaining Change 13 standard services without marking
    `SV17-C13-GAP` supported, committing, pushing, running sanitizer, or
    inspecting hosted CI before Change 20.
75. The next Change 13 slice implements `$set_coverage_db_name` and
    `$load_coverage_db`. A typed string-bearing SimIR boundary selects a
    project-root-confined final database or loads the current fsim coverage
    schema. Loads validate stable declaration and instance models, accumulate
    matching bin/cross counts into a copy, and commit only after the complete
    merge succeeds; sampling progress and observation histories remain local
    to the active run. Focused evidence covers cumulative two-run 50-to-100
    percent behavior, transactional model mismatch, malformed schemas, path
    escape, runtime-state round trips, and interpreter/LLVM O0/O2 execution.
    Runtime-state schema 45 and native-object schema v111 intentionally replace
    their predecessors without migration. Continue the remaining Change 13
    services without marking `SV17-C13-GAP` supported, committing, pushing,
    running sanitizer, or inspecting hosted CI before Change 20.
76. Clean-context restart point: remain on `codex/v2` at implementation base
    `9c98813c1610fcb1a2236310dde5714388c3c7ab`; preserve the entire intentionally
    dirty Batch 165 worktree (250 changed/untracked paths at this checkpoint).
    Treat every stale non-Batch-165 conversational or agent context as
    non-actionable; derive work only from this Batch 165 section, the live
    Change 13 allocation, and verified repository state.
    Change 13 remains in progress. The coverage-database slice in item 75 is
    implementation- and evidence-complete: focused frontend, runtime, LLVM,
    coverage-application and artifact-phase tests pass; the final eight-worker
    exact-LLVM Debug incremental build completes all 14 relinks. WebKit
    formatting and repository whitespace pass, the diagnostic catalog covers
    2,226 production codes, all 884 authored sources satisfy the source budget,
    and the SystemVerilog inventory reports 26 supported, 4 active and 5
    deferred rows with 18 preserved, 3 active and 4 physical width rows.
    Do not rerun this slice before beginning useful next work unless live files
    have changed.
77. Resume Change 13 by closing one bounded remaining standard-service family.
    The unimplemented families found in the live audit are `$exit`,
    `$isunbounded`, sampled/global-clock value functions, and the remaining
    coverage control/query/merge/save APIs. `$isunbounded` is the likely next
    small slice, but it must be implemented as compile-time symbolic-unbounded
    semantics, not as a runtime test or a direct-call-only special case. The
    lexer already tokenizes both `$isunbounded` and the standalone `$` as
    identifiers, and queue dimensions already special-case `$`; ordinary call
    parsing currently leaves `$` as an identifier. Preserve `$` through
    parameter defaults/overrides and specialization so both `$isunbounded($)`
    and `$isunbounded(P)` for `parameter P = $` return one-bit two-state true,
    while bounded constant parameters return false. Reject illegal arithmetic
    or value use of the symbolic unbounded value. Start discovery at
    `VerilogParser::parse_primary` in
    `src/frontend/verilog_parser_expressions.cpp`, the constant evaluators in
    `src/elaboration/elaboration_constants.cpp` and
    `src/elaboration/elaboration_sv_constants.cpp`, and parameter
    specialization/identity handling; use the codebase-memory graph before
    text search. Add direct `$`, parameter-default, parameter-override,
    generate-condition, false-result and cataloged-negative evidence, then
    interpreter/LLVM O0/O2 and artifact/cache identity coverage as required by
    the resulting representation. Bump current schemas and reject predecessors
    only if the owning serialized representation changes; never add migration
    compatibility. Continue without commit, push, sanitizer or hosted-CI
    inspection before Change 20.
78. The `$isunbounded` Change 13 slice is complete. The SystemVerilog constant
    model retains standalone `$` as an explicit symbolic-unbounded value through
    implicit parameter defaults, overrides, substitution, specialization and
    generate evaluation. `$isunbounded($)` and `$isunbounded(P)` return one-bit
    two-state true for symbolic values and false for bounded constants; unary,
    binary, typed-parameter and other ordinary value uses reject with cataloged
    diagnostics. Procedural uses are folded by the common constant evaluator and
    materialized as constant SimIR loads, so no runtime-only semantic path was
    introduced. Versioned `svconst-v3` identities include an explicit bounded/
    unbounded field and keep symbolic and bounded specializations/cache inputs
    distinct; the VPI canonical decoder accepts bounded v3 values. Frontend
    expressions continue to serialize `$` as the existing identifier form and
    the symbolic value is elaboration-internal, so no frontend, runtime-state or
    native-object schema bump is required.
    Focused evidence covers direct `$`, defaults, `$` and bounded overrides,
    generate selection, false results, typed/arithmetic/arity negatives,
    deterministic runtime-state round trips, specialization identity, and
    interpreter plus LLVM O0/O2 cold/warm execution. The final warning-clean
    eight-worker exact-LLVM Debug build passes the 12-test frontend,
    elaboration, expression/plusarg application, library/object/design/runtime
    artifact, cache, diagnostic, source-budget and inventory gate. The constant
    evaluator is 2,448 lines after moving value-query services into the existing
    service owner; repository whitespace also passes. Continue Change 13 with
    one bounded remaining service family without marking `SV17-C13-GAP`
    supported, committing, pushing, running sanitizer or inspecting hosted CI
    before Change 20.
79. The `$exit` Change 13 slice is complete with program-local lifecycle
    semantics. Every static and dynamically spawned SimIR process retains its
    stable elaborated program-instance owner. Explicit `$exit` terminates the
    current program and its background fork tree without affecting other
    programs; ordinary completion of all initial processes performs the same
    implicit exit. Only the last exited program requests simulation termination,
    after which pending module work is discarded and final procedures run.
    Module-context use and arguments reject with cataloged diagnostics. The
    existing `Halt` kernel boundary carries one program-exit bit, so interpreter
    and LLVM executors return through the same lifecycle implementation.
    Owning-unit schema 25, semantic-state schema 3, runtime-state schema 46 and
    native-object schema v112 intentionally replace their predecessors without
    migration compatibility. Focused evidence covers `$exit`/`$exit()`, invalid
    arity and module context, explicit plus implicit exits across two program
    instances, fork-background cancellation, pending module work, final
    execution, deterministic runtime-state round trips, interpreter and LLVM
    O0/O2 cold/warm execution, and cache identity for ordinary versus
    program-exit halts. The warning-clean eight-worker owning build and focused
    frontend, LLVM, expression application, library/object/design/runtime
    artifact, diagnostic and source-budget gate pass 9/9. Continue Change 13
    with one bounded remaining service family without marking `SV17-C13-GAP`
    supported, committing, pushing, running sanitizer or inspecting hosted CI
    before Change 20.
80. Batch 165 Change 13 is complete in the same intentionally dirty worktree.
    The final sampled-value family implements exact packed `$sampled`, `$rose`,
    `$fell`, `$stable`, `$changed` and governed-depth `$past`, direct scalar
    explicit clocking events, and scalar `$past` gating through one preponed
    slot snapshot and clock/gate-qualified history domain. The complete global
    family implements `$past_gclk`, `$rose_gclk`, `$fell_gclk`, `$stable_gclk`,
    `$changed_gclk`, `$future_gclk`, `$rising_gclk`, `$falling_gclk`,
    `$steady_gclk` and `$changing_gclk`. Beginning-of-slot functions use the
    retained history; future functions compare the preponed snapshot with the
    settled live signal. Interpreter and LLVM O0/O2 cold/warm execution share
    the same host-boundary service. Runtime-state schema 47 and native-object
    schema v114 replace their predecessors without migration; cache identity
    distinguishes kind, depth, clock, edge and gate. Focused expression,
    artifact, LLVM, diagnostic, source-budget and inventory gates pass 6/6
    after warning-clean eight-worker builds. The functional-coverage audit
    confirms start/stop, type/instance queries, database selection/load,
    transactional merge and final save were already complete. `SV17-C13-GAP`
    is now supported. Preserve Changes 1-13 and begin Change 14 without commit,
    push, sanitizer or hosted-CI inspection before Change 20.
81. Batch 165 Change 14 is complete in the same intentionally dirty worktree.
    DPI and VPI enum descriptors now own exact arbitrary-width `PackedLogic4`
    literals instead of projecting through 64-bit host integers; 129-bit
    signed and four-state X/Z round trips plus wrong-width, duplicate and
    two-state-unknown rejection pass. Live VPI publication distinguishes
    interface and program instances, publishes top-level packages with owned
    classes and exact signed 129-bit class-property ranges, and exposes stable
    assertion objects with persistent success/failure/vacuous/disabled/aborted
    callbacks carrying copied kind, name, process, instance, slot, source and
    action-suppression metadata. Portable VPI checkpoint schema 2 preserves
    exact wide enum identity while remapping handles. Existing exact range,
    value, strength, memory, callback, control and checkpoint behavior remains
    covered by the application/runtime matrices. The eight-worker focused
    build is warning-clean; frontend, runtime, VPI application, diagnostic
    catalog, source-line-budget and gap-inventory gates pass 6/6 in 0.75
    seconds, and `git diff --check` is clean. `SV17-C14-GAP` is supported and
    `SVW-VPI-ENUM` is preserved, leaving 28 supported/2 active/5 deferred
    residual rows and 19 preserved/2 active/4 physical width rows. Preserve
    Changes 1-14 and begin Change 15 without commit, push, sanitizer or hosted-
    CI inspection before Change 20.
82. Batch 165 Change 15 is complete in the same intentionally dirty worktree.
    The existing UVM integration path is now audited as one shared source,
    hierarchy, scheduling, callback, debugger and trace route rather than a
    parallel compatibility implementation. The warning-clean eight-worker UVM
    phase/TLM owning build succeeds, and the phase/TLM matrix, gap inventory,
    source harness, platform contract, conformance inventory and documentation
    gates pass 6/6 in 0.20 seconds. The composed release audit reaches all UVM
    component gates; its only mismatch is the intentionally deferred Batch 165
    diagnostic-count synchronization assigned to Change 19. `SV17-C15-GAP` is
    supported, leaving 29 supported/1 active/5 deferred residual rows. Preserve
    Changes 1-15 and begin Change 16 without commit, push, sanitizer or hosted-
    CI inspection before Change 20.
83. Batch 165 Change 16 is complete in the same intentionally dirty worktree.
    LLVM materializes arbitrary-width exact Logic9 constants into four native
    integer planes and sends wide signal operations through the existing exact-
    signal SimIR callback. Compiled debugger reads and writes preserve every
    word of all four register planes. A 129-bit repeating `UX01ZWLH-` compiler
    differential passes at O0/O2, and a 129-bit VHDL `std_logic_vector` passes
    through compiled signal execution into a debugger-visible local with
    interpreter parity. Warning-clean eight-worker owning builds succeed; LLVM
    plus application, multiple-root, SystemC, mixed conversion, typed-boundary,
    VCD, Tcl and C API gates pass 13/13. `SV17-C16-GAP` is supported and
    `SVW-LOGIC9-ENGINE`/`SVW-LOGIC9-DEBUG` are preserved, leaving 30 supported/
    0 active/5 deferred residual rows and 21 preserved/0 active/4 physical
    width rows. Preserve Changes 1-16 and begin Change 17 without commit, push,
    sanitizer or hosted-CI inspection before Change 20.
84. Batch 165 Change 17 is complete in the same intentionally dirty worktree.
    The existing complete artifact route retains wide X/Z source, portable
    object/library, design HIR/SimIR, runtime checkpoint and standalone state
    through relocation, replay and cold/warm execution with deterministic
    schema rejection. The native-cache differential now adds 137-bit Logic9
    upper-word `U` and `W` constants, proving distinct cold objects and an exact
    warm hit beside the existing four-state X/Z proof. Native object schema 115
    invalidates code generated before wide Logic9 support. The warning-clean
    eight-worker LLVM build succeeds; LLVM, portable-library, object/design
    artifact, application artifact-phase and runtime checkpoint gates pass 6/6.
    Preserve Changes 1-17 and begin Change 18 without commit, push, sanitizer
    or hosted-CI inspection before Change 20.
85. Batch 165 Change 18 is complete in the same intentionally dirty worktree.
    The new authoritative SystemVerilog release-closure matrix owns 51 rows:
    30 supported language/integration rows and 21 preserved width paths, with
    153 positive/negative/execution witness cells. Fourteen unique registered
    CTests span 17 governed direct/interpreter/LLVM/cache/debug/VCD/artifact/
    relocation/replay/checkpoint/multiple-root/UVM/mixed/public stages beneath
    6-GiB, 1,000-delta, 64-trace-signal and 7,200-second ceilings. The static
    audit passes, and the serial runner passes 14/14 in 165.34 seconds while
    retaining one verbose log per witness and a result ledger. Its first run
    found stale container expectations: the 137-bit packed aggregate fixture
    now uses an explicit nominal cast, the expected compiled count is three,
    and debugger key checks use arbitrary-width signed extraction rather than
    `low_word()`. The corrected container witness passes independently in
    75.61 seconds before the successful complete rerun. Preserve Changes 1-18
    and begin Change 19 without commit, push, sanitizer or hosted-CI inspection
    before Change 20.
86. Batch 165 Change 19 is complete in the same intentionally dirty worktree.
    Diagnostics, source, license, authored-test, feature/evidence and runtime-
    owner inventories are synchronized at 2,228, 885, 1,021, 330, 1,294,
    5,176 and 144 respectively; the feature matrix owns 617 exact evidence
    paths. The SystemVerilog gap, arbitrary-width and release-closure digests
    are `a2f19c56e715ea0f8198a672d96d08d0d9accd8eb7569f16bc6e542fc294ff40`,
    `f661b219e251e6369750ab406b19adf9c193cfb9570baaa0fdeab4f7984bad93`
    and `f4e8dcdfb60362544e6958449fa2a1e852cedcbba2ef6e929d5fb0a3aee1d124`.
    The synchronized language, feature, architecture, VPI, DPI, UVM tutorial,
    evidence-authoring and release-audit documents explicitly keep physical
    address-space, work, trace, scalar-format and ABI ceilings separate from
    SystemVerilog legality. The broad 34-test diagnostic/source/inventory/
    documentation/installed/platform/portability/composed-release contract
    passes in 21.37 seconds. Preserve Changes 1-19 and begin Change 20's fresh
    clean-first exact-LLVM Debug/Release eight-worker builds; do not run
    sanitizer or inspect hosted CI at this boundary.
87. Batch 165 Change 20 is complete. Fresh clean-first exact-LLVM 22.1.8 Debug
    and Release eight-worker builds complete 779 steps warning-free in
    11:05.59 and 9:10.78 at 4,997,832 and 2,250,920 KiB peak RSS with zero
    swaps. Complete Debug and Release regressions pass 142/142 in 7:51.79 and
    6:30.42 wall time, with CTest totals of 471.79 and 390.42 seconds, peak RSS
    of 3,788,408 and 3,793,688 KiB, and zero swaps. The first Debug regression
    passed the SystemVerilog closure matrix 14/14 before exposing two contract
    defects: a program fixture that executes `$finish` must report `stopped`,
    and active-region program clocking support/sampler processes legitimately
    retain program ownership. The corrected expectation preserves the exact
    `1010` value; removing the over-broad validator restores direct live-state
    reconstruction and artifact round-trip. Focused application/elaboration
    gates, both closure matrices and both complete regressions pass after the
    repair. Exact Change 20 correction ranges pass the WebKit formatting gate
    and repository whitespace is clean. No sanitizer or hosted-CI inspection
    ran. Commit and push the sole Batch 165 implementation, then create and
    push the documentation-only Batch 166 restart checkpoint and clear context
    before any Batch 166 implementation.

## Batch 164 planned restart checkpoint - 2026-08-10

1. Start in `/home/colin/projects/fsim`, read this file and the authoritative
   expanded Batch 164 allocation in `implementation_plan_v2.md`, and verify
   branch `codex/v2` is clean and synchronized at Batch 163 implementation
   `890122e6018fa2d371036d4cede5311247a398bf` plus this documentation-only
   Batch 164 planning checkpoint.
2. This checkpoint implements the required pre-batch flow. Batch 164 is
   expanded into exactly twenty changes without broadening the locked
   Verilog-2005 residual-language closure scope. No Batch 164 implementation
   file has changed; clear context after pushing this plan and resume only from
   this section and the authoritative allocation.
3. Preserve Batch 163's VHDL-2008/embedded-PSL clause inventory, arbitrary-
   width VHDL bit-string and IEEE numeric/fixed behavior, sequential/concurrent
   scheduling, force/release driver identity, hierarchy/configuration,
   access/file/protected objects, PSL clocks/properties/directives, mixed-
   language/debug/trace/VHPI integration, artifacts and public documentation at
   pushed commit `890122e`.
4. Batch 163's fresh clean-first exact-LLVM 22.1.8 Debug and Release
   eight-worker builds complete 741 steps warning-free in 10:19.03 and 8:39.05
   with peak RSS 4,286,220 and 3,017,072 KiB and zero swaps. Complete Debug and
   Release regressions pass 126/126 in 6:16.02 and 5:28.32 with peak RSS
   3,785,680 and 3,766,280 KiB and zero swaps.
5. The final explicit VHDL-labelled matrices pass 36/36 in 37.77 and 35.80
   seconds. The documentation/release slice passes 13/13, the exact post-format
   driver correction slice passes 4/4 in both configurations, the new Batch 163
   sources and Change 20 correction ranges pass the WebKit gate, and the
   repository whitespace check is clean.
6. The Batch 163 release baseline is 2,138 diagnostics, 835 bounded C/C++
   sources, 955 SPDX-owned files including `.clang-format`, 316 authored
   test/control files, 1,279 executable feature rows, 5,116 linked evidence
   cells, 604 exact paths split 265/312/27 test/production/release, 138 runtime
   files, and 36 corpus CTests.
7. Its exact SHA-256 identities are feature matrix
   `e38217f0ced26556c13e4ef6d4f32aa4d46172192ec54e926e739d165abbbab7`,
   evidence
   `2d9a2bf8fdc360b6b484087848eeb00def2a61cff5fecfacf9400fadcec2010b`,
   VHDL/PSL gap inventory
   `0a60ca24775a1e5e7282059b282557d36656e1f926d4207f1f3df48f4c8b891b`,
   and VHDL/PSL closure
   `169bc75fd1d0713fcc5760da8a22fe12376f0e655b26510566e33d78a336de35`.
8. Batch 164's cross-cutting correctness requirement is to inventory and remove
   every arbitrary Verilog bit-string or based-number literal-width cap. Exact
   source-determined width, signedness and X/Z bits must survive parsing,
   folding, elaboration, both engines, public boundaries, artifacts, caches and
   relocation beyond host-word size. Retain only explicit host-addressability
   or governed resource ceilings, diagnose those as physical limits rather
   than language illegality, and prove behavior above every former cap.
9. Changes 1-4 create the authoritative IEEE 1364-2005 clause and width-limit
   inventories, then close lexical/numeric, preprocessing/directives and
   declaration/data-object rules with exact positive, negative, diagnostic,
   provenance and wide-value evidence.
10. Changes 5-8 close expression sizing/signing, hierarchy/generate/configuration,
    gate/UDP and switch/strength/charge behavior, preserving arbitrary-width
    values and deterministic bounded resolution.
11. Changes 9-12 close continuous and procedural assignments, force/release,
    event/timing/scheduler statements, tasks/functions and standard system
    services with exact delta/race semantics and wide actual/result behavior.
12. Changes 13-16 close memories and `$readmem*`/`$writemem*`, specify paths/
    timing checks/pulses, VPI/host integration, and mixed VHDL/SystemVerilog/
    SystemC plus debugger/trace/public surfaces. New SDF application remains
    owned by Batch 170; Batch 164 supplies stable SDF-ready timing identity.
13. Changes 17-19 preserve every new construct through artifacts, libraries,
    checkpoints, caches, relocation and replay; run the zero-gap governed
    cross-engine/platform/resource matrix; and synchronize diagnostics, docs,
    tutorials, matrices, counts, digests, contracts and this handoff.
14. Change 20 runs fresh clean-first exact-LLVM Debug/Release eight-worker
    builds, full regressions, governed Verilog/mixed-language matrices, artifact
    and release gates, then creates the sole Batch 164 implementation commit/
    push. Use 120-minute command timeouts, at least eight build workers, serial
    governed stages and explicit process address-space/work limits; retain
    timing, RSS, swap, transcript, diagnostic and trace evidence.
15. Accumulate Changes 1-20 in one intentionally dirty Batch 164 worktree. Do
    not reset, commit or push implementation before Change 20. Batch 164 is not
    a sanitizer or hosted-CI monitoring boundary; do not run either unless a
    newly observed failure requires it. After the implementation commit, save
    and push the exact Batch 165 restart plan and clear context before Batch 165
    implementation.
16. Next action after this checkpoint is pushed and context is cleared:
    implement Batch 164 Change 1 only. Start with the clause-indexed IEEE
    1364-2005 and literal-width-limit inventory, reconcile it against current
    code and executable evidence, assign Changes 2-16 one-to-one owners, freeze
    supported/unsupported/deferred scope, and make duplicate, missing, unowned
    or drifted active rows fail a registered contract.
17. A user-approved future-scope amendment is now authoritative for Batch 166:
    support the legacy non-standard Synopsys `ieee.std_logic_signed`,
    `ieee.std_logic_unsigned`, `ieee.std_logic_arith`, and
    `ieee.std_logic_misc` compatibility packages. Preserve their historical
    logical-library names but label their provenance accurately, cover their
    overloads and executable behavior without host-word narrowing, diagnose
    ambiguous interaction with `numeric_std`, and retain package identity
    through libraries, artifacts, relocation, and caches.
18. Batch 164 Change 1 is complete in the intentionally dirty worktree. The
    authoritative 37-row IEEE 1364-2005 TSV freezes 19 reviewed supported
    baselines, 15 active residual rows owned one-to-one by Changes 2-16, and
    three explicit Batch 170/post-v2 deferrals. Its separate 15-row literal-
    width ledger freezes four preserved vector paths, eight active obligations,
    and three physical resource/host boundaries with exact source anchors.
    The registered contract rejects duplicate IDs/anchors, missing or
    misplaced owners/evidence, absent or repeated closures, clause/deferral/
    disposition drift, lost source anchors, plan/registration drift, and
    failure escapes. Inventory SHA-256 identities are
    `eac3e15d38a6749a71f8e243c87861ab9c4b9b19cf09ef71d4b991ae34c08f39`
    and `8c2eb522c5a3e4ed851ed7bf1edbb2e203a99adef40e9cbceb01544d22c3c4db`.
19. The exact-LLVM Debug tree regenerates and links 19 targets warning-free
    with eight workers in 28.97 seconds at 4,244,580 KiB peak RSS and zero
    swaps. The contract passes directly and through CTest, and the complete
    static contract range excluding only the long UVM execution matrix passes
    36/36 in 20.73 seconds at 57,288 KiB peak RSS and zero swaps. The new
    machine contract and two TSVs advance the SPDX-authored inventory from 955
    to 958 while diagnostics remain 2,138, bounded C/C++ sources 835, and
    authored test/control owners 316. Preserve all Change 1 and Batch 166 plan
    edits without reset, commit, push, sanitizer, or hosted-CI inspection.
    Next implement Batch 164 Change 2's lexical and arbitrary-width numeric-
    literal closure only.
20. Batch 164 Change 2 is complete in the same intentionally dirty worktree.
    The lexer and packed semantic engine already retained complete token text
    and word-vector values; the repaired frontend output shortcut was the one
    remaining `uint64_t` reparse. Its portable limb integer now handles exact
    binary/octal/decimal/hex multiply-add, sized and unsized selection,
    truncation, signed two's-complement interpretation, decimal rendering, and
    declared widths beyond `size_t` without allocating from the width. Focused
    frontend evidence covers values above `uint64_t`, four-base 257-bit
    equivalence, signed/truncating/unsized cases, 4097-bit declared width, and
    X/Z rejection. The display application proves matching interpreter/LLVM
    O0/O2 output, while elaboration/runtime evidence proves exact four-base
    packed values, signed bits, 4097-bit parameter identity, and a 4097-bit
    interpreter signal. Warning-free eight-worker focused builds complete, the
    direct inventory contract passes, and the combined frontend, inventory,
    release, elaboration, and display slice passes 6/6 in 9.99 seconds. The
    clause inventory is now 20 supported/14 active/3 deferred and the width
    ledger is six preserved/six active/three physical, with SHA-256 identities
    `cc1d9bee24cfa18873d64da67acade396da301641711d0f740b9b6f3caa93baa`
    and `8abbc3a8cbf1aa1834a28a52ea7e7b51ecff9c9001139b05bd8b1b632f53429a`.
    `git diff --check` is clean. Preserve Changes 1-2 and the Batch 166
    Synopsys-package amendment without reset, commit, push, sanitizer, or
    hosted-CI inspection. Next implement Batch 164 Change 3 preprocessing and
    compiler-directive closure only.
21. Batch 164 Change 3 is complete in the same intentionally dirty worktree.
    The existing preprocessor already retained the macro argument/default,
    substitution, paste/quote, recursive-expansion, conditional, include-search,
    provenance, line-control, directive-state, dependency-snapshot and cache
    surfaces. Two nonstandard include-boundary conditional restrictions are
    removed so included text can open, continue or close the surrounding
    conditional stream. Unknown pragmas have no effect; plaintext protect
    markers are transparent; encrypted `begin_protected` payload is contained
    and diagnosed as requiring an unavailable decryption provider, with exact
    malformed/nested/unmatched policy. Retired include-restriction diagnostic
    identities remain reserved by explicit non-emitting production constants
    to preserve the 2,138-entry catalog ABI and forbid reassignment.
    Warning-free eight-worker focused builds are current, and the exact
    frontend, elaboration, application, SystemVerilog-HIR and preprocessing
    application slice passes 5/5 in 30.79 seconds. The rebuilt frontend plus
    exact inventory/release contract slice passes 5/5 in 9.36 seconds,
    including the release candidate. The clause inventory is now 21
    supported/13 active/3 deferred while the width ledger remains six
    preserved/six active/three physical, with SHA-256 identities
    `d0f507d0a96baefa7857c3e6561c32b0f05c287ffe9851d674f3b27aec3b58cb`
    and `8abbc3a8cbf1aa1834a28a52ea7e7b51ecff9c9001139b05bd8b1b632f53429a`.
    Preserve Changes 1-3 and the Batch 166 Synopsys-package amendment without
    reset, commit, push, sanitizer, or hosted-CI inspection. Next implement
    Batch 164 Change 4 declarations and data-object closure only.
22. Batch 164 Change 4 is complete. Its first slice replaces the shared
    declaration/range evaluator's rejection of every logic literal wider than
    64 bits. Verilog based literals now use the arbitrary-width packed constant
    service, followed by a checked signed-64 value conversion only where an
    integer-only range consumer requires it. A 257-bit hexadecimal spelling
    whose value is four elaborates an exact five-bit signal. The initial full
    elaboration host exposed and then verified preservation of the shared VHDL
    scalar-character path; the final host passes 1/1 in 1.18 seconds after a
    warning-free eight-worker rebuild. `VLW-LEGACY-INTEGER` is therefore
    preserved, advancing the width ledger to
    seven preserved/five active/three physical rows with SHA-256
    `76a0da465c12f1a7289469073ce1ff898908f514cb8fed300521601dfdcb8f27`;
    The completed declaration matrix covers ANSI ports, signed and wide ranges,
    parameters/localparams, nets, regs, integer, time, real/realtime, events,
    genvars, explicit callable lifetime and source language, plus exact wide
    initialization. Verilog attributes parse at compilation-unit, module,
    parameter, port and object boundaries with no unknown-tool effect and
    stable empty-name/value/closing diagnostics. One-dimensional Verilog
    memories are legal and retain their packed element plus exact unpacked
    range expressions; multidimensional and net memories diagnose separately.
    Ordinary semantic-HIR type references now retain exact executable width and
    four-state identity, and the Verilog application build retains a 257-bit
    parameter plus static memory before executing memory write/read behavior.
    Warning-clean eight-worker focused builds and the frontend, elaboration and
    application hosts pass. Diagnostic, source-line, resource, VHDL/PSL, UVM
    and release-inventory contracts pass with 2,142 production codes.
    `VL05-C04-GAP` advances the clause inventory to 22 supported/12 active/3
    deferred rows with SHA-256
    `cea73020834d1cda611a268c944bb2c9c44563b0079eb574fa5a851c0ece999c`;
    the width ledger remains seven preserved/five active/three physical at
    SHA-256
    `76a0da465c12f1a7289469073ce1ff898908f514cb8fed300521601dfdcb8f27`.
    Preserve the accumulated worktree and the Batch 166 Synopsys-package
    amendment without commit, push, sanitizer, or hosted-CI inspection. Next
    implement Batch 164 Change 5 expression semantics only.
23. Batch 164 Change 5 is complete. Checked packed-to-scalar conversion now
    accepts arbitrary declared widths only when their known value is exactly
    zero- or sign-extended into the requested host scalar; fixed/associative
    container indices and keys preserve all words. LLVM process frames retain
    process-specific register widths and flattened word offsets without
    changing the public v1 frame structure. Generated code uses exact-width
    integers beyond 64 bits and explicit eight-byte plane alignment, which
    fixes the O2 aligned-store fault exposed by the new 257-bit frame test.
    Application adapters reconstruct every plane word, and interpreter plus
    compiled O0/O2 cold/warm runs preserve a 257-bit static-memory index.
    Direct LLVM evidence covers every arithmetic family, signed/unsigned
    comparisons, four-state equality, logical/reduction/count operations,
    514-bit concatenation, conditional selection, all shifts/rotates, and
    static/dynamic bit and part selections/inserts. Verilog-2005 implicitly
    static functions are now eligible for the restricted constant-function
    evaluator; a 257-bit function result retains its high bit. A WebKit-formatted
    warning-clean 173-step eight-worker focused build passes the frontend,
    elaboration, runtime, LLVM and two application hosts 6/6 in 19.01 seconds;
    diagnostic, source-line, resource and Verilog inventory gates pass 4/4.
    `VL05-C05-GAP` advances the clause inventory to 23 supported/11 active/3
    deferred rows at SHA-256
    `5e86ef5f1925c561a5b5e275ff094bd619ad1b730c55ed4a5dc7f24136552051`.
    `VLW-LOW-WORD` and `VLW-ENGINE-PLANES` advance the width ledger to nine
    preserved/three active/three physical at SHA-256
    `fb31418a076ffa87b9df2380753f77e0aec224414f9246af7101278807e44119`.
    Preserve the accumulated Changes 1-5 worktree and the Batch 166 Synopsys
    package amendment without commit, push, sanitizer, or hosted-CI inspection.
    Next implement Batch 164 Change 6 hierarchy and elaboration closure only.
24. Batch 164 Change 6 is complete in the same accumulated dirty worktree.
    Verilog-2005/SystemVerilog now parse structured `defparam` declarations in
    direct and generated module bodies, preserve indexed hierarchy segments,
    and route direct, owner-prefixed, deep descendant, instance-array and
    generated targets through specialization before hierarchy publication.
    Cataloged negatives cover malformed paths, unresolved descendants,
    duplicate/conflicting overrides, indexed parameter leaves, local parameters
    and illegal language crossings. SystemVerilog packed range bounds retain
    arbitrary-width intermediate constants until the final checked signed
    range consumer, proven by a 257-bit high-bit parameter deriving distinct
    executable child widths and versioned cache identities. Owning-unit schema
    18 round-trips structured declarations, and a dedicated merged application
    case proves interpreter/LLVM equality plus cold/warm analysis and native
    cache reuse. A WebKit-formatted warning-clean 97-step eight-worker focused
    rebuild plus the application-case rebuild passes frontend, elaboration,
    library and specialization/cache hosts 4/4. Diagnostic, source-line,
    resource and Verilog inventory contracts pass 4/4 with 2,151 production
    codes. `VL05-C06-GAP` advances the inventory to 24 supported/10 active/3
    deferred rows at SHA-256
    `182fdff35fec835c1a03b36bddec11f52239c227d0960b08ab5fd8d88be781d3`;
    the width ledger remains nine preserved/three active/three physical at
    SHA-256
    `fb31418a076ffa87b9df2380753f77e0aec224414f9246af7101278807e44119`.
    The clean broader application run initially exposed an accumulated
    class/multiple-root LLVM mismatch: `left.tree.child.leaf` supplied a 32-bit
    constructor actual while the kernel boundary requested 64 bits. Packed
    class constructor, property and method values now explicitly request the
    executor frame's native width; class object handles remain fixed at 64
    bits. The warning-clean 220-step dependency rebuild, 102-step runtime host,
    runtime suite, isolated class integration, dedicated specialization case
    and full application core all pass after the repair. Preserve the
    accumulated Changes 1-6 worktree and the Batch 166 Synopsys-package
    amendment without commit, push, sanitizer, or hosted-CI inspection. Next
    implement Batch 164 Change 7 only.
25. Batch 164 Change 7 is complete in the same accumulated dirty worktree. The
    production audit confirmed that all assigned logic/buffer/tristate gate and
    combinational/sequential UDP semantics were already implemented, including
    table level/wildcard/binary/edge symbols, initialization, previous state,
    no-change, arrays, generated and library instances, one/two/three-value
    delays, strengths, X/Z matching, artifacts, relocation, and caches. The
    application core now asserts the time-ordered buf/not/and/nand/or/nor/xor/
    xnor trace, including delayed buffer events and intermediate X propagation,
    instead of checking only final known values. Frontend, elaboration, library
    artifact, application core, and transition-delay hosts pass; the latter
    retains exact tristate, UDP delay, cold/warm compiled, object/design,
    mapping, debugger, and VCD evidence. `VL05-C07-GAP` advances the inventory
    to 25 supported/9 active/3 deferred rows at SHA-256
    `c1961ad44ceff00a5f9ff4363a1a3d34608cf59d73addf7f520a20c771a3a487`;
    the width ledger remains nine preserved/three active/three physical at
    SHA-256
    `fb31418a076ffa87b9df2380753f77e0aec224414f9246af7101278807e44119`.
    Preserve accumulated Changes 1-7 and the Batch 166 Synopsys package
    amendment without commit, push, sanitizer, or hosted-CI inspection. Next
    implement Batch 164 Change 8 only.
26. Batch 164 Change 8 is complete in the same accumulated dirty worktree. The
    parser/HIR and runtime already covered all pull, MOS/CMOS, transmission,
    conditional/resistive, charge-storage and convergent network families, but
    new timed nmos/pmos/rpmos/cmos/rcmos evidence found that an unresolved
    variable source contributed no resolved-driver-table strength. Its correct
    value was therefore silently assigned high-Z strength. The shared runtime
    strength query now treats the current value of an unresolved source as a
    default-strong contribution before resistive reduction. HDL-visible timed
    samples prove all five MOS families conduct `11111` and disconnect to
    `ZZZZZ` identically in interpreter and compiled cold/warm execution. The
    existing resolution case continues to prove tran/rtran/conditional cycles,
    X controls, rank reduction, retained/zero/finite/renewed and packed charge,
    decay scheduling, serialized topology, debugger/VCD, artifacts, relocation
    and mapped libraries at O0/O2. Runtime and resolution hosts pass after a
    warning-clean eight-worker rebuild. `VL05-C08-GAP` advances the inventory
    to 26 supported/8 active/3 deferred rows at SHA-256
    `fe25d7fde61774691ffdd491e10fb6957e168395204fe3fda09973301bf9e075`;
    the width ledger remains nine preserved/three active/three physical at
    SHA-256
    `fb31418a076ffa87b9df2380753f77e0aec224414f9246af7101278807e44119`.
    Preserve accumulated Changes 1-8 and the Batch 166 Synopsys package
    amendment without commit, push, sanitizer, or hosted-CI inspection. Next
    implement Batch 164 Change 9 only.
27. Batch 164 Change 9 is complete in the same accumulated dirty worktree.
    Comma-separated continuous net assignments now retain every assignment
    beneath their shared strength/delay and reject malformed empty/trailing
    entries. Runtime-base indexed part-select continuous targets are legal and
    no longer capped at 64 bits. Delayed targets use the new append-only
    `WriteInertialDynamicPartSlice` SimIR alternative; zero-delay targets use
    the existing dynamic-part update with arbitrary-width selection. Runtime
    and LLVM dynamic selection/insertion preserve exact packed planes beyond a
    host word.

    The append-only JIT ABI adds `execute_signal_operation` at offset 592 and
    grows from 592 to 600 bytes without moving an existing field. Exact-width
    reads, wide blocking initialization, and wide whole/static/dynamic update
    and inertial writes delegate to the application executor, while narrow
    callbacks remain unchanged. The transition-delay application proves
    129-bit delayed and zero-delay dynamic slices inside 257-bit drivers,
    rejected pulses, accepted rise/turnoff/X transitions, exact zero-delay
    ordering, X/Z planes, VCD/debugger equality, cold/warm caches, and all 19
    processes compiled in both O0 and O2. Direct runtime and LLVM tests cover
    the same wide selection/scheduling primitives; existing resolution
    coverage retains multiple-driver, strength, topology and artifact proof.

    The WebKit-formatted 243-step eight-worker dependency build is
    warning-clean. The focused frontend, elaboration, LLVM, runtime,
    transition-delay, resolution, catalog, source, resource and inventory
    gates pass. `VL05-C09-GAP` advances the inventory to 27 supported/7
    active/3 deferred rows at SHA-256
    `f8493844f27266d329854d0dfede3f8fa223ffb52db8d675c3de7ac214c0b8b4`;
    the width ledger remains nine preserved/three active/three physical at
    SHA-256
    `fb31418a076ffa87b9df2380753f77e0aec224414f9246af7101278807e44119`.
    Preserve accumulated Changes 1-9 and the Batch 166 Synopsys-package
    amendment without commit, push, sanitizer, or hosted-CI inspection. Next
    implement Batch 164 Change 10 only.
28. Batch 164 Change 10 is complete in the same accumulated dirty worktree.
    Verilog-2005 and SystemVerilog now retain procedural assign/deassign as
    distinct HIR statements, accept force/release in the classic language,
    and recursively parse concatenated lvalues. Lowering evaluates each RHS
    and dynamic concatenated target once, before any distributed blocking
    write, and supports ordinary, selected and concatenated blocking,
    nonblocking, delayed, force and release targets. Each procedural
    continuous assignment owns an explicit activation signal and reactive
    force driver; replacement/deassign deactivate all structurally identical
    target drivers, and deassign copies the effective value into underlying
    storage before release.

    Wide signal operations no longer fall back or reject solely because the
    target exceeds a host word. The exact callback executes whole, static-
    slice, dynamic-bit and dynamic-part blocking, update, delayed, inertial,
    force and release operations. The application evidence covers 129- and
    257-bit drivers, selected and concatenated procedural assignments,
    replacement, masked stored writes, captured dynamic target indices,
    reactive updates, deassign preservation, active-driver debugger
    `(forced)` visibility, exact VCD output, and interpreter/cold-LLVM/warm-
    LLVM equality with all 29 processes compiled at O0 and O2. Negative
    evidence covers malformed assign/deassign syntax and automatic-local
    targets.

    The direct wide-driver literals exposed and corrected a missed Change 2
    execution path. Runtime literal materialization now expands arbitrary-
    width binary, octal, decimal and hexadecimal digits, including X/Z/? fill,
    without a `uint64_t` parse. The application evidence now uses direct 129-
    and 257-bit hexadecimal literals rather than chunked concatenations; the
    existing 257/4,097-bit binary/octal/decimal/hex elaboration evidence remains
    green.

    Signal/output validation was split into a WebKit-formatted 606-line
    helper, leaving `llvm_jit_validation.cpp` at 1,969 lines. The full
    109-step eight-worker build is warning-clean. Frontend, elaboration, LLVM,
    runtime, object/design/library artifact, procedural-assignment,
    transition-delay, resolution, diagnostics-catalog, source-budget,
    resource and inventory gates pass. `VL05-C10-GAP` advances the inventory
    to 28 supported/6 active/3 deferred rows at SHA-256
    `aec5f7af03d4c544dea705291da20ad8049d6e8effeddb0553ed98c93a7199fe`;
    the width ledger remains nine preserved/three active/three physical at
    SHA-256
    `fb31418a076ffa87b9df2380753f77e0aec224414f9246af7101278807e44119`.
    Preserve Changes 1-10 and the Batch 166 Synopsys-package amendment without
    commit, push, sanitizer or hosted-CI inspection. Next implement Batch 164
    Change 11 only.
29. Batch 164 Change 11 is complete in the same accumulated dirty worktree.
    Verilog-2005 procedural `for` is no longer incorrectly gated as
    SystemVerilog, while inline declaration in that loop remains a cataloged
    language-mode negative. `disable name` is retained in frontend/HIR and
    lowers to an exact local jump, named-fork site cancellation, or dynamically
    active named-sequential-block interval. Named forks remain addressable
    after `join_none`; same-parent unrelated children survive. A fork child can
    terminate an enclosing named sequential block, redirect its parent and
    cancel only descendants inside the interval; nested same-process and
    inactive-target behavior is also covered. Hierarchical callable and cross-
    scope disable/return behavior remains owned by Change 12.

    Runtime schema 20 serializes fork sites and named-block intervals.
    Compiler validation, cache identity and lowering retain both operations,
    and interpreter plus cold/warm LLVM O0/O2 application evidence is equal
    through artifact round trips, debugger state and VCD. Existing event,
    delay/repeat control, scheduler-region, race, cancellation, time-overflow
    and bounded-delta contracts remain green. The warning-clean affected build
    completes 278 steps with eight workers; the focused frontend, elaboration,
    LLVM, runtime and application slice passes 9/9 in 48.00 seconds, and the
    artifact, diagnostic, source, resource and inventory contract slice passes
    8/8. The exact Change 11 C/C++ surface passes the repository WebKit dry-run
    and whitespace is clean.

    `VL05-C11-GAP` advances the inventory to 29 supported/5 active/3 deferred
    rows at SHA-256
    `c73ea4d1b36c1992e0887a1bac8abe5ed464c56d789ea0c80133e3353d571a21`;
    the width ledger remains 9 preserved/3 active/3 physical at SHA-256
    `fb31418a076ffa87b9df2380753f77e0aec224414f9246af7101278807e44119`.
    Preserve Changes 1-11 and the Batch 166 Synopsys-package amendment without
    commit, push, sanitizer or hosted-CI inspection. Next implement Batch 164
    Change 12 only.
30. Batch 164 Change 12 is complete in the same accumulated dirty worktree.
    Dynamic callable stacks now execute direct and indirect recursion without a
    fixed frame-depth limit. Automatic SystemVerilog functions/tasks and VHDL
    procedures isolate exact packed arguments, locals, results and suspended
    contexts per invocation; static callable storage remains shared. Named
    disable/return unwinds the correct active frame, and process storage remains
    reference-stable as recursive frames are appended.

    Formatted string/file output and file scanning now consume arbitrary-width
    packed planes, including 129/137-bit values and X/Z digits, without indexing
    executor storage by register ID or truncating to the ABI low word.
    Unconstrained `std::randomize` generates deterministic arbitrary-width known
    packed values directly instead of enumerating `2^width`; constrained calls
    still use the governed solver and preserve transactional resource failures.
    Wide integral `$fseek` arguments undergo the specified 32-bit API conversion
    instead of a 64-bit admission rejection. VHDL standard logic/string and
    composite paths touched by the Change 12 closure also retain exact values
    beyond one host word.

    Runtime state schema 21 and native object schema 87 retain the new callable
    and service behavior through artifacts and cold/warm caches. The full
    incremental build completes 59 steps warning-free with eight workers. The
    focused runtime, LLVM, elaboration and recursive/wide application slice
    passes 13/13 in 85.76 seconds; the artifact, HIR, cache, diagnostic, source,
    resource and inventory slice passes 9/9 in 1.84 seconds. The exact late
    Change 12 C/C++ surface passes the WebKit dry-run and `git diff --check` is
    clean.

    `VL05-C12-GAP` advances the inventory to 30 supported/4 active/3 deferred
    rows at SHA-256
    `25f6b8d1d484dca1c7ab54785551c56bea0a72471fbba40ec0dec4c3a4ac7242`;
    the width ledger remains 9 preserved/3 active/3 physical at SHA-256
    `fb31418a076ffa87b9df2380753f77e0aec224414f9246af7101278807e44119`.
    Preserve Changes 1-12 and the Batch 166 Synopsys-package amendment without
    commit, push, sanitizer or hosted-CI inspection. Next implement Batch 164
    Change 13 only.
31. Batch 164 Change 13 is complete in the same accumulated dirty worktree.
    IEEE 1364-2005 file and memory services no longer inherit SystemVerilog-only
    parser or elaboration gates. Fixed memories retain exact arbitrary-width
    packed values through reads, writes, bit/part selects and selected blocking
    updates. The compiled container callbacks exchange the complete packed
    register rather than only the ABI low word. A constant memory-word input
    port uses a transaction-sensitive bridge: `$readmem*` and later procedural
    writes publish a memory transaction after the container commit, and the
    bridge rereads the exact word without depending on process initialization
    order. Packed Verilog `$fgets` and `$ferror` destinations are exact and EOF
    preserves the destination.

    Interpreter and cold/warm LLVM O0/O2 application evidence covers 137-bit
    descending memories, X/Z digits, sparse addresses, selected assignment, a
    memory-word module port, `$readmem*`/`$writemem*`, standard file services,
    malformed-input rollback, runtime-state round trips, sandboxing and governed
    file/work ceilings. Runtime-state schema 23 and native-object schema 89
    retain the memory-transaction operation field through artifacts and caches.
    The complete eight-worker incremental tree builds warning-clean. Frontend,
    diagnostics, elaboration, LLVM, file application, runtime, object/design
    artifact and artifact-phase gates pass 9/9 in 65.11 seconds; the Verilog
    inventory contract also passes.

    `VL05-C13-GAP` advances the inventory to 31 supported/3 active/3 deferred
    rows at SHA-256
    `d3221a80927a3df4f5c90190d2b4d29e7cef3ec776f98c72a3ca37fbc81cf62b`;
    `VLW-MEMORY-WORD` advances the width ledger to 10 preserved/2 active/3
    physical rows at SHA-256
    `e317fe0103f222e1cad87c6f30b45d49e0354ef007336089e2656b9ad587a56e`.
    Preserve Changes 1-13 and the Batch 166 Synopsys-package amendment without
    commit, push, sanitizer or hosted-CI inspection. Next implement Batch 164
    Change 14 only.
32. Batch 164 Change 14 is complete in the same accumulated dirty worktree.
    Normalized specify paths and timing checks now own stable, instance-qualified
    SDF-ready identities rather than relying on dense build-order IDs or source
    paths. Runtime construction and artifact restoration reject empty or
    duplicate identities, and runtime-state schema 24 carries them through
    round trips and mapped-library relocation without pulling SDF application
    forward from Batch 170.

    Existing parser, elaboration and runtime coverage retains module/specparam
    paths, conditions, edge sensitivity, destination data sources, all standard
    delay-table shapes, PATHPULSE selection, `pulsestyle`, `showcancelled`,
    cancellation, overlapping recovery, every timing-check family and notifier
    updates. New direct evidence fixes the exact open-window behavior at both
    setup/hold boundaries and deterministic same-tick reference/data ordering.
    The application drives a 137-bit Verilog-2005 port through a 72-bit full-path
    slice and proves exact values, callbacks, VCD and stable identities across
    interpreter, debug, cold/warm LLVM O0/O2, runtime-state reload and relocated
    mapped libraries.

    The exact Change 14 surface is WebKit-formatted and `git diff --check` is
    clean. The complete 389-step eight-worker dependent build is warning-clean;
    frontend, diagnostics, inventory, resource, elaboration, LLVM, runtime,
    specify application, object/design artifact and artifact-phase gates pass
    11/11 in 39.51 seconds. `VL05-C14-GAP` advances the inventory to 32
    supported/2 active/3 deferred rows at SHA-256
    `bdddd135da7f479e8458971d4e763829db75e9fcb94e88e171bb8fbe1f055ee4`;
    the width ledger remains 10 preserved/2 active/3 physical at SHA-256
    `e317fe0103f222e1cad87c6f30b45d49e0354ef007336089e2656b9ad587a56e`.
    Preserve Changes 1-14 and the Batch 166 Synopsys-package amendment without
    commit, push, sanitizer or hosted-CI inspection. Next implement Batch 164
    Change 15 only.
33. A user-approved roadmap amendment inserts two batches before the v2 ABI and
    artifact freeze. Batch 172 replaces fsim's custom SystemC kernel with the
    pinned Accellera SystemC 3.0.2 reference implementation while retaining
    only fsim-specific source/artifact frontends, public compatibility shims and
    data/execution integration. It also supplies native in-island TLM 1.0/2.0,
    one shared runtime, an opaque transport-neutral backend with stable
    serializable identities and deterministic time/delta/island/sequence
    messages, and a worker-loopback gate. No raw Accellera pointers or
    coroutine/channel state may escape that boundary. The in-process backend
    must therefore remain replaceable by a post-v2 kernel-per-worker process
    backend without changing the public SystemC/TLM/SCV or artifact ABI; the
    actual partitioner and parallel scheduler remain post-v2.

    Batch 172 also requires complete trace and debugger visibility for every
    supported SystemC signal, port, export, clock, resolved channel and alias.
    Port names map onto canonical bound-channel identities; post-update dirty
    batches preserve time, delta, region and sequence without drops through the
    internal trace, VCD and FST. Selective and late tracing take an immediate
    snapshot, backpressure is bounded and lossless, debugger writes occur only
    at safe points, TLM activity is a correlated transaction stream rather than
    fabricated signal changes, and unsupported custom channels are explicit or
    provide the documented observation adapter.

    Batch 173 adds SCV 2.0.1. Vendor the official archive with exact source,
    license/notice/SBOM and patch digests; attempt an unmodified SystemC 3.0.2
    build first, then carry only minimal reviewed compatibility patches with
    rationale, tests and removal criteria. One shared SCV library must cover
    smart pointers, constraints/distributions/bags, deterministic randomization,
    extensions introspection, transaction streams/generators/attributes/
    relations, plug-ins, caches, relocation and Linux/Windows ABI checks.
    Transaction records use the same pointer-free backend and deterministically
    correlate with TLM and waveform time/delta identity.

    Batches 172 and 173 are sanitizer and hosted-CI monitoring boundaries. Each
    Change 20 owns the LLVM-disabled sanitizer, full Debug/Release, upstream and
    fsim regression/examples, installed/relocation, plug-in/ABI, mixed-language,
    signal/port trace, transaction/debug, worker-loopback, determinism,
    performance, memory, backpressure, teardown and failure-containment gates,
    followed by one commit/push and repair of all non-documentation CI jobs.
    The former Batches 172-175 are renumbered 174-177: ABI/artifact/migration
    freeze, cross-platform/performance qualification, release-candidate
    packaging/documentation and final `v2.0.0` release. This documentation-only
    amendment does not alter the intentionally dirty Batch 164 Changes 1-14
    implementation checkpoint: preserve it exactly and next implement Change
    15 only, with no commit, push, sanitizer or hosted-CI inspection.
34. Batch 164 Change 15 is complete in the same accumulated dirty worktree.
    Every live simulation owns a generation-checked VPI hierarchy covering
    Verilog roots, modules, generated scopes, ports, nets, variables, named
    events, processes, exact parameters, fixed memories and driver objects.
    Packed ranges, declared net kinds, scalar profiles, scalar strengths,
    driver slices, exact arbitrary-width values and X/Z planes survive live
    publication. Dedicated signal, driver, container and named-event hooks keep
    aggregate values, individual contributions, memory words, aliases and
    callbacks synchronized even when a driver change does not alter resolution.

    Lifecycle ordering brackets process execution, finish runs final blocks,
    stop/resume remains distinct, unsupported reset is rejected, and
    deposit/force/release changes are transactional across the VPI registry and
    kernel. Time profiles preserve exactly representable resolutions; foreign
    simulation handles and stale generations reject. Mixed VHDL-backed
    Verilog/SystemVerilog aliases consistently use the current Logic4 VPI
    boundary profile rather than advertising a Logic9 type for a collapsed
    value. The former arbitrary one-megabit descriptor ceiling is removed;
    checked arithmetic retains only the explicit `uint32_t` VPI width ABI.

    The eight-worker application target builds warning-clean. The full
    `fsim.application` gate passes 1/1 in 27.65 seconds after crossing the mixed
    hierarchy that previously exposed the category mismatch; focused
    `fsim.application.vpi`, `fsim.runtime` and `fsim.elaboration` pass 3/3 in
    1.42 seconds. The exact Change 15 surface passes the WebKit dry-run and
    repository whitespace is clean. `VL05-C15-GAP` advances the inventory to
    33 supported/1 active/3 deferred rows. `VLW-VPI-VECTOR` now records the
    descriptor repair under B164-C15 while the width ledger remains 10
    preserved/2 active/3 physical. Their SHA-256 identities are
    `c6527dfe4b3ff33d9ff6b41db1a4aa9e839a49adb118107fd93f06bb855a31e0`
    and `d438f6d356b54f70183bf7febaff87ee01f8dac1434bf69f2311cc8443da5476`.
    Preserve Changes 1-15 and the Batch 166 Synopsys-package amendment without
    commit, push, sanitizer or hosted-CI inspection. Next implement Batch 164
    Change 16 only.
35. Batch 164 Change 16 is complete in the same accumulated dirty worktree. A
    focused 137-bit four-state value crosses SystemVerilog, VHDL,
    `sc_lv<137>`, VHDL and SystemVerilog without losing X/Z planes. The same
    fixture proves explicit and inferred bindings, unrelated and reordered
    roots, interpreter and cold/warm compiled execution, debugger display,
    time/delta-stamped callbacks, exact VCD output and stable wide specify-path
    identity. A focused mixed-Logic9 regression proves Verilog VPI descriptors
    and values consistently project the shared backing signal to Logic4.

    Public C, Tcl and actual CLI/VCD tests now exercise exact 137-bit X/Z
    deposit, force, release or trace paths. VPI instance publication also
    synthesizes intervening generate scopes beneath the nearest enclosing VPI
    instance. Repeated project-root aliases retain distinct
    `selected.lane[0].u` module chains instead of colliding as duplicate `u`
    roots, while explicit foreign-parent boundaries preserve their established
    flattened pseudo-root representation. The public API regression proves
    both alias-specific parent chains and the two-child root inventory.

    The affected targets build warning-clean with eight workers. Focused Tcl
    and API tests pass 2/2 in 1.56 seconds. The broad application, specify,
    VHDL Logic9, mixed-conversion, typed-boundary and API tests pass 6/6 in
    37.59 seconds. `VL05-C16-GAP` closes the clause inventory at 34 supported,
    zero active and three deferred rows; `VLW-TRACE-PUBLIC` advances the width
    ledger to 11 preserved, one active artifact/cache obligation and three
    physical host boundaries. Their SHA-256 identities are
    `338b64ba883f6243d9f799d31c22f873e871a5977bbb9fc9c45ae3b5ea738c84`
    and `f296b3d4182c0964ac4444b6e844f6ef79e16b612516a065af3b4caad4a2891e`.
    Preserve Changes 1-16 and the Batch 166 Synopsys-package amendment without
    commit, push, sanitizer or hosted-CI inspection. Next implement Batch 164
    Change 17 only.

36. Batch 164 Change 17 is complete in the same accumulated dirty worktree.
    Native cache schema 90 hashes packed width, Logic9 identity, every
    `aval`/`bval` word and every nine-state symbol for load constants and all
    container-predicate constant sites. Direct 137-bit tests distinguish
    upper-word values and X from Z while retaining exact warm hits.

    Design-artifact schema 4 now serializes and validates the complete
    SystemVerilog executable HIR rather than only classes: units, declarations,
    types, expressions, statements, processes and classes survive standalone
    reload. That repairs VPI parameter publication from `.fsimdesign` without
    producer files. Portable objects/libraries preserve exact 257/137-bit text,
    signedness and X/Z; VPI checkpoints preserve signed 137-bit stored and
    forced planes. Mapped-library upper-word edits invalidate cache identity.
    Interpreter, cold/warm LLVM, child-directory relocation, standalone CLI and
    VCD replay retain exact wide values after source/object producers are
    hidden, while malformed and future payloads reject.

    A warning-clean 22-step exact-LLVM Debug incremental build completes with
    eight workers. The library, LLVM, main application, artifact, class and
    runtime gate passes 6/6 in 77.16 seconds; the strengthened LLVM cache gate
    passes independently in 40.30 seconds. Direct and CTest inventory contracts
    pass, WebKit formatting and repository whitespace are clean.
    `VLW-ARTIFACT-CACHE` closes the width ledger at 12 preserved/zero active/
    three physical rows. The unchanged clause and completed width SHA-256
    identities are
    `338b64ba883f6243d9f799d31c22f873e871a5977bbb9fc9c45ae3b5ea738c84`
    and `4d0924571f33e54f8d77d66979e290fe223c183eb0bc963fdbffb841429a424e`.
    Preserve Changes 1-17 and the Batch 166 Synopsys-package amendment without
    commit, push, sanitizer or hosted-CI inspection. Next implement Batch 164
    Change 18 only.
37. Batch 164 Change 18 is complete in the same accumulated dirty worktree.
    `verilog_release_closure.tsv` freezes 46 rows: 34 supported IEEE
    1364-2005 clauses plus 12 preserved width paths, with 138 exact witness
    cells mapped to 23 registered CTests and 17 governed execution stages. The
    serial runner retains one log per witness and a result ledger, while its
    transcript owners enforce 6-GiB address-space, 1,000-delta, 64-signal VCD
    and 7,200-second matrix boundaries. The composed audit rejects missing
    witnesses/stages, escapes and contract drift and composes the diagnostic,
    source, SPDX, v1 conformance and portability gates.

    The source-budget gate required a coherent constructor split:
    `application_simulation.cpp` is now 1,989 lines and the existing setup-TPP
    seam owns the moved setup body. Full execution exposed three previously
    latent publication defects. Duplicate generated procedural process names
    now retain the first natural name and use stable `$process_<runtime-index>`
    fallbacks. Bidirectional transmission processes remain topology and no
    longer project nonexistent Driver/Transaction records; projection
    validation rejects stale phantom records while directional MOS drivers
    remain readable. Fixed Verilog memories now suppress only their internal
    selected-word signal aliases and publish canonical Memory children such as
    `memory[3]` and `memory[2]`.

    Focused procedural-assignment, resolution and SystemVerilog-file tests pass
    after eight-worker builds. The complete retained 23-witness closure matrix
    passes in 94.63 seconds. Its SHA-256 is
    `a80eea635da93dff681c7118cd3339e7b4bb679c83a2cf7137cc3eeccef6f39e`.
    Preserve Changes 1-18 and the Batch 166 Synopsys-package amendment without
    reset, commit, push, sanitizer or hosted-CI inspection. Next implement
    Batch 164 Change 19 only.
38. Batch 164 Change 19 is complete in the same accumulated dirty worktree.
    The installed public surface now contains `verilog-2005.md`, its
    producer-independent tutorial and its exact closure audit, enforced by the
    registered `fsim.verilog-documentation` contract and the staged-install
    audit. README, architecture, language support, VPI, feature/evidence,
    inventory, differential, public and resource records state the same width
    policy: source/context width, signedness and X/Z planes remain exact;
    `uint32_t` VPI descriptor addressability and configured memory/work/trace
    ceilings are physical boundaries rather than Verilog legality rules.

    The synchronized inventory is 2,152 diagnostics, 842 bounded C/C++
    sources, 972 SPDX-owned files and 320 test/control files. The feature
    matrix remains 1,279 rows/5,116 cells/604 paths with 138 runtime owners and
    36 corpus CTests. Removing the obsolete wider-than-64 runtime deferral
    intentionally advances its digest to
    `7f879238bfcdbc544e688c710038c97063a8464f9817c758b87c1a2f89e0472d`;
    the canonical evidence digest remains
    `2d9a2bf8fdc360b6b484087848eeb00def2a61cff5fecfacf9400fadcec2010b`.
    Verilog gap/width/closure identities remain
    `338b64ba883f6243d9f799d31c22f873e871a5977bbb9fc9c45ae3b5ea738c84`,
    `4d0924571f33e54f8d77d66979e290fe223c183eb0bc963fdbffb841429a424e`
    and
    `a80eea635da93dff681c7118cd3339e7b4bb679c83a2cf7137cc3eeccef6f39e`.

    The direct Verilog documentation and inventory/release-candidate gates
    pass, and the registered documentation, closure, v1 release,
    installed-public, MSVC/SystemC/tool/resource portability slice passes
    26/26 in 20.64 seconds. Repository whitespace is clean. Preserve Changes
    1-19 and the Batch 166 Synopsys-package amendment without reset, commit,
    push, sanitizer or hosted-CI inspection. Next run Batch 164 Change 20's
    clean-first Debug/Release and complete local release gates only.
39. Batch 164 Change 20 local qualification is complete. Fresh clean-first
    exact-LLVM 22.1.8 Debug and Release eight-worker builds are warning-free.
    Debug completes in 12:04.92 with 4,795,068 KiB peak RSS and zero swaps;
    Release completes in 10:18.77 with 3,266,400 KiB peak RSS and zero swaps.

    The first full Debug regression passed 131/132 and isolated its only
    failure to a 129-bit Logic9 JIT register-plane alignment fault in
    `vhdl_numeric`. The frame ABI stores planes as contiguous `uint64_t` words
    and guarantees eight-byte alignment, but LLVM inferred sixteen-byte
    alignment for wide plane-two and plane-three loads/stores. The four
    accesses now explicitly use eight-byte alignment. The direct compiler
    regression exercises three 129-bit Logic9 registers at offsets 0, 3 and 6
    using caller storage deliberately aligned to eight rather than sixteen
    bytes in both O0 and O2; adjacent i64-only Logic9 mask and result constants
    are width-polymorphic as well.

    Final Debug and Release regressions each pass 132/132. Debug reports
    534.37 seconds of CTest time, 8:54.38 wall time and 3,785,976 KiB peak RSS;
    Release reports 460.20 seconds of CTest time, 7:40.21 wall time and
    3,791,508 KiB peak RSS. Both record zero swaps. This batch is neither a
    sanitizer nor hosted-CI monitoring boundary. Run the final formatting,
    whitespace and documentation/release contract checks, then commit and push
    the one accumulated Batch 164 Changes 1-20 implementation. After that,
    save and push the documentation-only Batch 165 restart plan and clear
    context before any Batch 165 implementation.

## Batch 163 planned restart checkpoint - 2026-08-10

1. Start in `/home/colin/projects/fsim`, read this file and the authoritative
   expanded Batch 163 allocation in `implementation_plan_v2.md`, and verify
   branch `codex/v2` is clean and synchronized at Batch 162 implementation
   `25f6f83f222c3ed46b3cb53a0fe9bbb5a6967e6e` plus this documentation-only
   Batch 163 planning checkpoint.
2. This checkpoint implements the required pre-batch flow. Batch 163 is
   expanded into exactly twenty changes without broadening the locked
   VHDL-2008 and embedded-PSL digital-language closure scope. No Batch 163
   implementation file has changed; clear context after pushing this plan and
   resume only from this section and the authoritative allocation.
3. Preserve Batch 162's UVM 1.2/2020-3.1 object policies, synchronization,
   command-line/test runner, report/objection/factory/config/resource tracing,
   release compatibility/provenance, conformance inventory, project-owned
   smoke coverage, platform/public contracts, closure audit, and tutorial at
   pushed commit `25f6f83`.
4. Batch 162's fresh clean-first exact-LLVM 22.1.8 Debug and Release
   eight-worker builds complete 733 steps warning-free in 9:36.84 and 8:49.59
   with peak RSS 4,070,136 and 2,506,320 KiB and zero swaps. Complete Debug and
   Release regressions pass 122/122 in 6:24.90 and 5:26.29 with peak RSS
   3,777,748 and 3,783,548 KiB and zero swaps.
5. The final exact UVM 1.2 matrix passes all ten stages in 8:18.93 and UVM
   2020-3.1 in 10:20.25: 2/2 in 18:39.19 with peak RSS 4,860,744 KiB and zero
   swaps. All fourteen regenerated traces are 28,345 bytes with SHA-256
   `62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
   The final documentation/public/release contract slice passes 32/32 and the
   whitespace check is clean.
6. The Batch 162 release baseline is 1,279 executable feature rows, 5,116
   linked evidence cells, 604 exact paths split across 265 test, 312 production,
   and 27 release/build owners, 138 runtime files, 36 corpus CTests, 937 SPDX-
   owned files, 2,071 diagnostics, 826 bounded C/C++ sources, and 313 authored
   test/control owners. Its matrix/evidence digests are
   `70deacfc60402730e5a567494e74aa015b4759b3655983a2ff6464c97ef11d42`
   and `2d9a2bf8fdc360b6b484087848eeb00def2a61cff5fecfacf9400fadcec2010b`.
7. Changes 1-4 create the authoritative clause-indexed IEEE 1076-2008 and
   embedded-PSL inventory, then close lexical/design-unit, declaration/type,
   expression/overload, generic-package, configuration/context, and external-
   name gaps with exact positive, negative, runtime, diagnostic, and resource
   ownership.
8. Changes 5-8 close sequential timing/assignment/control, concurrent/process/
   postponed/guard/disconnect/driver behavior, block/generate/configuration
   elaboration, and access/file/protected/shared-object execution with stable
   scheduling, lifecycle, rollback, stale-owner rejection, and explicit work/
   storage ceilings.
9. Changes 9-12 add embedded PSL verification units, clocks, sequences,
   properties, endpoints, and directives; analyze temporal operators and
   bounds; execute sampled attempts with abort/vacuity/reset/end-of-run rules;
   and publish deterministic reports, coverage, callbacks, debug, traces,
   artifacts, limits, and negative evidence.
10. Changes 13-16 integrate the residual VHDL/PSL behavior with current IEEE/
    VITAL ownership, mixed SystemVerilog/SystemC boundaries, multiple roots,
    debugger/callback/waveform/activity/VHPI visibility, and versioned object/
    design artifacts, libraries, checkpoints, caches, relocation, and replay.
    New SDF ownership remains deferred to Batch 170.
11. Changes 17-19 require zero unresolved supported rows in the clause-indexed
    conformance inventory, then run direct/interpreter/LLVM O0/O2/cache/debug/
    trace/relocation/replay/root/mixed/platform evidence and synchronize all
    diagnostics, matrices, docs, tutorials, inventories, installed/public
    contracts, audits, counts, digests, and this handoff.
12. Change 20 runs fresh clean-first exact-LLVM Debug/Release eight-worker
    builds, complete regressions, governed VHDL/PSL and mixed-language
    matrices, cross-engine/cache/debug/trace/relocation evidence, every audit,
    installed/public/platform/portability contract, and release-candidate gate
    before the sole implementation commit/push.
13. Accumulate Changes 1-20 in one dirty Batch 163 worktree. Use at least eight
    workers for local builds, retain exact test/memory/transcript/trace output,
    update this handoff after every completed change, and do not reset, commit,
    or push the implementation before Change 20. Serial governed runs and
    explicit RSS/work ceilings must prevent any return of over-20-GiB behavior.
14. Batch 163 is not a sanitizer or hosted-CI monitoring boundary. Do not run a
    sanitizer or inspect hosted CI unless a new failure requires it. After the
    Batch 163 implementation commit, expand and push the exact Batch 164 restart
    plan and clear context before Batch 164 implementation.
15. Next action after this checkpoint is pushed and context is cleared:
    implement Batch 163 Change 1 only. Begin with the clause-indexed IEEE
    1076-2008 and embedded IEEE 1850 PSL inventory, reconcile it against current
    parser/analyzer/elaborator/runtime and release evidence, assign positive,
    negative, execution, diagnostic, and resource owners, freeze supported/
    unsupported/deferred scope, and make duplicate, missing, or unowned active
    rows fail a registered contract.
16. Change 1 is complete in the intentionally dirty Batch 163 worktree. The
    authoritative 33-row TSV freezes 14 supported IEEE 1076-2008 baselines
    covering clauses 2 through 15, 15 unsupported closure rows owned one-to-one
    by Changes 2 through 16, and four explicit Batch 170/post-v2 deferrals. Its
    fixed 15-column schema requires existing parser, analyzer, elaboration,
    runtime, positive, negative, execution, diagnostic, and resource paths for
    every row. The registered contract rejects duplicate IDs, empty scope,
    unknown boundaries, misplaced/missing owners, absent/repeated closure
    owners, missing clause baselines, scope drift, and failure escapes.
17. The exact-LLVM Debug tree regenerates warning-clean with eight workers and
    no compilation. The direct and registered inventory checks pass, and the
    complete static diagnostic/source/license/inventory/conformance/
    documentation/public/portability/release slice passes 33/33 in 9.18
    seconds. Two new SPDX-authored release files advance the repository count
    from 937 to 939; diagnostics remain 2,071, bounded C/C++ sources 826, and
    authored test/control owners 313. The whitespace check is clean. Preserve
    Change 1 without reset, commit, push, sanitizer, or hosted CI. Next implement
    Change 2's remaining lexical and design-unit behavior only.
18. The authoritative roadmap now carries an explicit cross-cutting requirement
    for both upcoming digital-language batches: Batch 164 must remove arbitrary
    Verilog bit-string/based-number width limits, and Batch 165 must do the same
    for SystemVerilog bit-string, based-number, unbased, and unsized literals.
    These are language-correctness requirements, not requests for a larger
    replacement constant: preserve exact values beyond host-word width, retain
    only host-addressability or governed resource ceilings with distinct
    diagnostics, and prove parsing-through-execution/artifact behavior above
    every former implementation cap.
19. Change 2 is complete in the same intentionally dirty worktree. VHDL-2008
    basic and extended identifiers, doubled-quote strings, character literals,
    delimiters, nested comments, library/use/context clauses, context
    declarations, trailing-clause recovery, library-scoped primary/secondary
    identity, duplicate admission order, and exact `2008`/`08` project selection
    now have focused positive and negative evidence. Bit-string decoding accepts
    B/O/X/D, UB/UO/UX, and SB/SO/SX without a host-word semantic cap: a
    requested-width ring validates lossless truncation, null/zero-length results
    are retained, decimal conversion uses arbitrary-precision 32-bit limbs, and
    65-/129-/257-/4,097-bit witnesses pass. Non-host-addressable materialization
    failures remain a physical-representation diagnostic rather than a language
    width rule.
20. The final Change 2 incremental exact-LLVM Debug build completes 22 steps
    warning-free with eight workers in 32.72 seconds, peak RSS 4,070,388 KiB,
    and zero swaps. Frontend/project/expression/analysis-order tests pass 4/4;
    the complete VHDL label passes 33/33 in 33.89 seconds at peak RSS 194,780
    KiB and zero swaps; and the static diagnostic/source/license/inventory/
    conformance/documentation/public/portability/release slice passes 33/33 in
    19.54 seconds. Counts are 939 authored files, 2,078 diagnostics, 826 bounded
    C/C++ sources, and 313 authored test/control owners. Preserve Changes 1-2
    without reset, commit, push, sanitizer, or hosted CI. Next implement Change
    3's remaining declarations and types only.
21. Change 3 is complete. Its first slice implements same-region
    incomplete-type completion, recursive access identity, integer record
    members, and structural ownership for user-defined attribute declarations/
    specifications plus group templates/instances. Duplicate/uncompleted
    incomplete types, duplicate/undeclared attributes, and duplicate/unresolved
    groups receive exact cataloged diagnostics. Deferred package constants now
    accept arbitrary constrained subtypes, require a structurally conforming
    body completion, merge its initializer, and retain completion provenance.
    Attributes and groups propagate through nested declarative regions and
    publish explicit semantic-HIR declarations/profiles; portable owning-unit
    schema 15 retains the new fields. Warning-clean 390-step and 27-step eight-
    worker builds complete, focused frontend/library/elaboration/application/
    catalog/source/inventory evidence passes 7/7, the full VHDL label passes
    33/33, and the live catalog is 2,093. Next execute Change 4, beginning with
    its mandatory arbitrary-width IEEE numeric correction. Preserve the entire
    dirty Changes 1-3 worktree without commit, push, sanitizer, or hosted-CI
    inspection.
22. Batch 163 Change 4 has a mandatory wide-IEEE-numeric correction. Remove the
    arbitrary 64-bit result/source/inference limits in `numeric_std`,
    `numeric_bit`, `fixed_generic_pkg`, and `fixed_pkg`; do not substitute a
    larger constant. Wide `to_signed`, `to_unsigned`, `resize`, shifts/rotates,
    fixed conversion/resizing, and ordinary arithmetic must remain exact for
    every representable constrained width. A wide `to_integer` operand is legal
    when its value fits fsim's predefined integer range, so diagnose unknown or
    out-of-range values rather than rejecting from operand width. Require
    65-/129-/257-bit and larger non-power-of-two elaboration, interpreter, LLVM
    O0/O2, native-cache, and negative integer-range/resource evidence. Preserve
    Change 3 ordering: finish declarations/types before starting this Change 4
    expression work.
23. Change 4's mandatory arbitrary-width IEEE numeric/fixed correction is
    complete, while the rest of Change 4 remains in progress. Numeric and fixed
    inference/lowering now accept every positive SimIR-representable constrained
    width; fixed constant construction and scale shifts use arbitrary-width
    packed values rather than host-word masks. Wide `to_integer` checks unknown
    state and predefined-integer value fit by narrowing/restoring the complete
    operand. The adjacent packed-generic and VHDL qualification/conversion 64-
    bit gates are also removed. Application evidence covers 65, 129, 257, and
    521 bits in `numeric_std`, `numeric_bit`, and the `fixed_generic_pkg`/
    `fixed_pkg` chain through interpreter, LLVM O0/O2, and cold/warm native
    cache, with unknown, out-of-range, and representation-resource negatives.
    A warning-clean 17-step eight-worker full dependency build and the complete
    VHDL label passing 33/33 validate the accumulated Changes 1-4 worktree.
    Continue Change 4's remaining expression/name/reusable-unit allocation;
    do not commit, push, run sanitizer, or inspect hosted CI before Change 20.
24. The next Change 4 expression slice adds general VHDL conditional
    expressions without consuming top-level conditional/selected waveform
    delimiters. Architecture constants and executable parenthesized waveform
    values retain `?:` HIR; VHDL static evaluation selects only the chosen
    alternative, and constrained vectors stay in arbitrary-width packed form
    rather than entering the former `int64_t` specialization path. The residual
    packed-scalar helper no longer rejects an operand merely because its width
    exceeds 64; it diagnoses only unknown digits or a value that cannot fit the
    scalar environment. Runtime lowering uses real branches, proven by an
    unselected divide-by-zero alternative, and 129-bit declaration/execution
    evidence passes interpreter plus compiled-engine O0/O2. A warning-clean
    21-step eight-worker dependency build, focused frontend/expression/numeric/
    fixed gates passing 4/4, and the full VHDL label passing 33/33 validate this
    slice. Preserve the dirty Changes 1-4 worktree and continue Change 4 with
    case expressions and the remaining name/reusable-unit closure; do not
    commit, push, run sanitizer, or inspect hosted CI before Change 20.
25. Change 4 now also accepts bounded VHDL-2008 case expressions. Scalar
    choices, `|` lists, directional ranges, exhaustive Boolean alternatives,
    and final `others` lower into the branch-short-circuited conditional form;
    malformed separators, repeated/nonfinal `others`, and nonexhaustive bounded
    forms have cataloged diagnostics. Frontend HIR covers static and executable
    cases, while the expression application covers a dynamic choice list, a
    static integer range, an arbitrary-width packed static alternative, and
    interpreter/compiled O0/O2 equality. Preserve the accumulated dirty
    Changes 1-4 worktree and continue Change 4 with name, overload, and
    reusable-unit closure; do not commit, push, run sanitizer, or inspect
    hosted CI before Change 20.
26. Change 4 now gives local and rooted dot-separated VHDL-2008 external signal
    names executable identity. The parser retains a distinct external-name HIR
    wrapper plus declared subtype metadata; qualified-package discovery skips
    that wrapper, and lowering resolves its target through the hierarchy signal
    map only after validating width, value domain, signedness, and nominal type.
    The expression application proves the rooted signal through interpreter and
    LLVM O0/O2 and rejects a mismatched external subtype with
    `FSIM-ELAB-VHEXTERNAL-001`; frontend/catalog coverage also diagnoses
    unsupported object classes and malformed delimiters. Preserve the dirty
    Changes 1-4 worktree and continue the remaining overload/reusable-unit
    audit before declaring Change 4 complete. Do not commit, push, run
    sanitizer, or inspect hosted CI before Change 20.
27. Batch 163 Change 4 is complete in the intentionally dirty worktree. The
    mandatory arbitrary-width numeric/fixed correction covers 65, 129, 257,
    and 521 bits; general conditional and bounded case expressions use
    branch-short-circuited execution; external signal names resolve real local
    or rooted identity and enforce their declared subtype. The existing
    overload, generic type/function/procedure/package/subprogram,
    package-instantiation, configuration, component, context, aggregate,
    conversion, physical, fixed, numeric, and analysis-order applications all
    remain green. Final validation is a warning-clean 28-step eight-worker
    dependency build, focused frontend/catalog/expression gates passing 3/3,
    and the complete VHDL label passing 33/33 in 30.77 seconds. Preserve the
    accumulated Changes 1-4 worktree and begin Change 5 sequential assignment,
    timing, and control semantics. Do not commit, push, run sanitizer, or
    inspect hosted CI before Change 20.
28. Change 5 is complete with executable VHDL-2008 sequential force/release.
    Default and explicit `in` modes produce Force/Release HIR and reuse the
    existing packed whole-signal/index/slice runtime operations with distinct
    `FSIM-ELAB-VHFORCE-*` diagnostics. The projected-waveform application
    proves a forced value masks an intervening ordinary update and release
    reveals that current driver value across interpreter and compiled O0/O2;
    frontend/catalog/projected gates pass 3/3. Explicit driving-value `out`
    mode now preserves process-driver identity with per-driver masks and
    append-only native callbacks; a second resolved driver proves `0` to `X`
    to `0` behavior through interpreter and cold/warm compiled execution.
    Immediate `'driving_value` assertions prove the forced overlay and the
    restored underlying driver. The clause-10 audit retains the existing
    wait/assert/report, assignment, procedure-call, loop/control, case,
    waveform, timing, cancellation, and delta evidence. The warning-clean
    eight-worker dependency build passes, focused frontend/catalog/C-ABI/LLVM/
    projected/runtime gates pass 6/6, and the complete VHDL label passes 33/33
    in 35.03 seconds. The repository-wide clang-format default is now the
    WebKit preset in `.clang-format`; the current environment supplies
    clang-format 22.1.8 and Change 9's new sources were formatted through that
    preset. Preserve the accumulated Changes 1-5 worktree and begin Change
    6 concurrent/process closure. Do not commit, push, run sanitizer, or
    inspect hosted CI before Change 20.
29. Change 6 is complete in the same intentionally dirty worktree. VHDL
    postponed processes, concurrent assertions, and concurrent procedure calls
    retain phase ownership from frontend through semantic HIR and SimIR and
    execute after ordinary active/update work; invalid reactive-plus-postponed
    runtime ownership is rejected. Architecture, block, and generate
    disconnection specifications retain explicit, `all`, and exclusion-aware
    `others` selection, exact type marks, and independent delays. The
    synthesized disconnect transaction now uses that delay rather than the
    guarded assignment's waveform delay. Focused evidence diagnoses type
    mismatch, duplicate/overlapping selection, and illegal postponed use, and
    executes pending-disconnect cancellation/restart, two distinct resolved
    driver owners, delayed fallback to the remaining driver, interpreter
    teardown isolation, and bounded concurrent nonconvergence. The complete
    elaboration executable passes; warning-clean eight-worker focused builds
    and frontend/library/object/design/catalog/application-artifact/projected/
    runtime gates pass 8/8, and the complete VHDL label passes 33/33 in 32.67
    seconds. The catalog contains 2,118 diagnostics. Preserve Changes 1-6 and
    begin Change 7 block, generate,
    configuration, and resolved-hierarchy closure without commit, push,
    sanitizer, or hosted-CI inspection before Change 20.
30. Change 7 is complete in the same intentionally dirty worktree. Existing
    for/if/case generate selection, locally static alternatives, nested block/
    generated configuration paths, lexical components, `all`/`others`/`open`,
    referenced configurations, and composed generic/port maps remain green.
    VHDL input expressions materialize owned child signals and reactive
    drivers; locally static array-element output actuals use owned formals and
    slice bridge processes. The configuration application proves a dynamic
    input expression, two child drivers resolving element zero to `X`, an
    independently driven element one retaining `1`, and interpreter plus LLVM
    O0/O2 cold/warm equality. Recursive child elaboration now checkpoints
    collection sizes and path/mutation ownership; any child diagnostic removes
    its signals, processes, specializations, names, configuration/interface/
    SystemC state, resolver insertions, and boundary-driver records without a
    full-design copy. A late recursive failure followed by a reused path and an
    independent boundary writer proves no duplicate-path or leaked-driver
    cascade. Warning-clean eight-worker focused builds and frontend/catalog/
    source/gap-inventory/elaboration/configuration/analysis-order/component
    gates pass 8/8; the final complete VHDL label passes 33/33 in 33.54 seconds.
    Preserve Changes 1-7 and begin Change 8 access, file, protected, and
    shared-object execution without commit, push, sanitizer, or hosted-CI
    inspection before Change 20.
31. Change 8 is complete in the same intentionally dirty worktree. VHDL access
    allocation uses a process/type-owned associative live heap and a separate
    append-only issued-handle ledger, so 32-bit identities are never reused.
    `Deallocate` removes a live designated value and nulls its variable; null
    deallocation is harmless, while null, stale, or foreign dereference fails
    before access. Both live and lifetime storage use explicit representation-
    derived ceilings. The shared SimIR file service retains closed identities,
    enforces a 4,096-open simulation lifetime ceiling, process ownership and
    read/write mode, and finalizes open streams. Existing VHDL direct/TextIO
    application evidence covers mode, `endfile`, read/write, close, aliases,
    and interpreter/compiled O0/O2 behavior; runtime evidence covers stale,
    unknown, closed, cross-owner, and finalization paths.

    Protected shared objects execute each wait-free method as one atomic
    source-ordered scheduler segment. Active reentry, nested protected
    procedure calls, and waits reject deterministically; an ordinary pure
    function cannot call an impure protected function. The former 64-bit
    private-member limit is gone, and all container default/write/conditional/
    predicate/reduction paths now operate over arbitrary packed word counts.
    Focused evidence executes a 137-bit private member, two-process atomic
    updates, reentry/wait/purity negatives, and fresh-simulation defaults. The
    complete eight-worker exact-LLVM Debug dependency build finishes 109 steps
    warning-clean. The full VHDL label passes 33/33 in 34.43 seconds; catalog,
    source, gap-inventory, elaboration, combined file application, advanced-
    type application, and runtime gates pass 7/7. The live catalog is 2,119,
    touched C/C++ sources remain below 2,000 lines, and `git diff --check` is
    clean. Preserve Changes 1-8 and begin Change 9 embedded PSL lexical,
    grammar, and declaration ownership. Do not commit, push, run sanitizer, or
    inspect hosted CI before Change 20.
32. Change 9 is complete in the same intentionally dirty worktree. The VHDL-
    2008 lexer now distinguishes case-insensitive `-- psl` markers from
    ordinary comments. Native and embedded `vunit`, `vprop`, and `vmode`
    library units retain source context, exact targets, and distinct kinds;
    entity, architecture, package, and verification-unit regions own default
    clocks, Boolean/sequence/property/endpoint declarations, formal profiles,
    and labeled assert/assume/restrict/cover directives. Entity statement parts
    accept concurrent embedded directives, and explicit native `assert
    property` is not confused with an ordinary VHDL assertion. Project analysis
    copies every retained record into frontend-independent semantic HIR with
    owned token spellings and interned spans. All nine `FSIM-VHDL-PSL-*`
    malformed/duplicate/placement diagnostics have focused negative evidence.

    The complete eight-worker exact-LLVM Debug dependency build finishes 161
    steps warning-clean. The final full VHDL label passes 33/33 in 34.33
    seconds; frontend, analysis-order, catalog, source-budget, gap-inventory,
    and release gates also pass. The live catalog is 2,128 and 828 authored
    C/C++ sources remain below the 2,500-line hard ceiling with a 2,000-line
    refactor target. Preserve Changes 1-9 and begin Change 10 PSL clock and
    temporal-expression analysis. Do not commit, push, run sanitizer, or
    inspect hosted CI before Change 20.

    An intentionally broader static probe also confirms two already-stale
    Change 19 synchronization owners: `fsim.v1-inventory-release` still freezes
    2,118 diagnostics rather than the live 2,128, and the Windows/resource ABI
    contracts still freeze `sizeof(fsim_jit_runtime_v1) == 568` despite earlier
    accumulated runtime-table growth. Do not rewrite those release baselines in
    Change 10; synchronize them with all other counts and public/platform
    contracts in Change 19, then require them in Change 20.
33. Change 10 is complete in the same intentionally dirty worktree. PSL
    analysis resolves visible scalar Boolean/bit/logic samples through owning
    and targeted VHDL units; retains local, inherited, referenced, and explicit
    clocks with canonical rising/falling-edge identity; rejects incompatible
    clocks; and records unknown-clock-as-no-edge plus sampled-unknown-as-false
    policy. Frontend and semantic HIR distinguish Boolean, sequence, property,
    endpoint, and static-integer-formal classes and retain typed concatenation,
    fusion, three repetition forms, both suffix implications, next/prev/
    eventually/always, until/before/within, endpoints, exact spans, and static
    ranges. Numeric formals remain symbolic with optional normalized defaults
    and are legal only inside locally static temporal bounds. Ten new
    `FSIM-VHDL-PSL-010` through `019` diagnostics cover clock, sample, type,
    bound, endpoint, name, override, cross-clock, and cycle failures.

    Focused frontend, project-analysis, catalog, source-budget, and PSL-gap
    gates pass. The complete eight-worker exact-LLVM Debug dependency build
    finishes 161 steps warning-clean in 3:48.16 with peak RSS 4,177,472 KiB and
    zero swaps; the full VHDL label passes 33/33 in 35.02 seconds with peak RSS
    194,780 KiB and zero swaps. The live catalog is 2,138 and 829 authored C/C++
    sources remain under the 2,500-line hard ceiling with a 2,000-line refactor
    target. Preserve Changes 1-10 and begin Change 11 PSL elaboration and
    execution without commit, push, sanitizer, or hosted-CI inspection before
    Change 20. The intentionally stale Change 19 inventory/ABI synchronization
    owners remain deferred exactly as recorded in item 32.
34. Batch 163 Change 11 is complete in the intentionally dirty worktree. The
    simulation retains analyzed VHDL PSL HIR and samples after scheduler update
    publication through one engine-neutral attempt service. Boolean and edge
    clocks share canonical histories; source-ordered overlapping attempts execute
    concatenation/fusion, all three repetition families, declaration defaults and
    actuals, within/implication/recurrence/strength/abort forms, exact finite
    completion, disable/reset cleanup, and unknown-no-edge policy. `[=]` retains
    its trailing non-match span while `[->]` ends on the chosen occurrence.
    Monitor, history, active/lifetime attempt, observation-evaluation, inner
    temporal-step, nesting, and conservative 512-MiB owned-storage ceilings bound
    CPU and memory. The live application produces 51 deterministic attempts
    across interpreter, debug, LLVM O0/O2, and cold/warm cache; runtime resource
    and lifecycle evidence remains green. A warning-clean 103-step eight-worker
    dependency build completed in 2:44.69 at 4,206,252 KiB maximum RSS with zero
    swaps, followed by a final warning-clean 13-step relink. The complete VHDL
    label passed 34/34 in 36.08 seconds at 194,608 KiB maximum RSS with zero
    swaps; focused frontend/runtime/application/catalog/source/inventory evidence
    passed 7/7. The repository-wide
    `.clang-format` continues to resolve the WebKit preset under clang-format
    22.1.8 and all touched Change 11 sources pass its dry-run check. Preserve
    Changes 1-11 and begin Change 12 without commit, push, sanitizer, or hosted-CI
    inspection before Change 20.
35. Batch 163 Change 12 is complete in the intentionally dirty worktree. Every
    retained PSL attempt now carries assert/assume/restrict/cover kind, stable
    unit/instance and semantic source-span identity, source-order slot, exact
    sample/time/delta coordinates, and pass/failure/vacuous/aborted outcome.
    Public simulation trace and coverage APIs expose 60 attempts from 20 live
    directives and separate all four outcome counters. Dedicated PSL and common
    assertion callbacks run in deterministic completion order; deliberately
    throwing first observers are contained without losing later publication.
    Assert/assume failures route as errors, restriction failures as warnings,
    and cover failures remain coverage-only: the differential retains 31 reports
    with exactly one warning. Interpreter, debug, LLVM O0/O2, and cold/warm cache
    traces/counters agree exactly. The warning-clean 97-step eight-worker build
    completed in 2:38.95 at 4,206,868 KiB maximum RSS with zero swaps. Existing
    SystemVerilog assertion plus focused contracts pass 8/8; the complete VHDL
    label passes 34/34 in 35.41 seconds at 194,572 KiB maximum RSS with zero
    swaps. WebKit formatting and whitespace checks are clean. Preserve Changes
    1-12 and begin Change 13 without commit, push, sanitizer, or hosted-CI
    inspection before Change 20.
36. Batch 163 Change 13 is complete in the intentionally dirty worktree. The live
    PSL design now uses resolved IEEE `std_logic` clocks/predicates, co-resides
    with a 129-bit `numeric_std` value plus physical/file/protected declarations,
    and samples a path-sensitive `VitalPathDelay` output at the stable scheduler
    boundary. All 21 directives publish 63 deterministic attempts across
    interpreter, debug, LLVM O0/O2, and cold/warm cache. Existing numeric, fixed,
    VITAL, file, protected, and physical applications remain green and own their
    specialized execution/negative matrices. Portable owning-unit schema 16
    round trips PSL default clock, declaration/formal/default, property,
    directive label, tokens, and source spans without reparsing; durable
    semantic-HIR/running-attempt artifacts remain Change 16. No SDF ownership was
    pulled forward from Batch 170. The focused integration slice passes 11/11,
    the eight-worker dependency tree is current with no work, and the full VHDL
    label passes 34/34 in 36.49 seconds at 194,604 KiB maximum RSS with zero
    swaps. WebKit formatting and whitespace checks are clean. Preserve Changes
    1-13 and begin Change 14 without commit, push, sanitizer, or hosted-CI
    inspection before Change 20.
37. Batch 163 Change 14 is complete in the intentionally dirty worktree. VHDL
    PSL monitors now expand over ordered `DesignIR` occurrences, qualify every
    clock/sample by exact instance path, and publish occurrence identity through
    attempts, coverage, and callbacks. Explicit `entity(architecture)` targets
    now select the named architecture rather than an earlier primary-name match.
    Duplicated and reversed VHDL roots plus colliding-name SystemVerilog and
    SystemC roots prove isolation and configured order. The governed
    SV-to-VHDL-to-SystemC graph proves converted ports, resolved values, and PSL
    sampling after stable delta propagation across interpreter, debug, LLVM
    O0/O2, cold/warm cache, and source edits. Simultaneous clock histories share
    one immutable observation map, removing quadratic retained-value copies
    while preserving resource preflight and the 512 MiB ceiling. The focused
    boundary slice passes 6/6. The warning-clean 84-step eight-worker build
    completes in 2:17.36 at 4,208,056 KiB peak RSS with zero swaps; the full
    VHDL label passes 34/34 in 37.15 seconds at 195,120 KiB peak RSS with zero
    swaps. Repository formatting defaults to WebKit and touched PSL sources plus
    whitespace checks are clean. Preserve Changes 1-14 and begin Change 15
    without commit, push, sanitizer, or hosted-CI inspection before Change 20.
38. Batch 163 Change 15 is complete in the intentionally dirty worktree. Every
    simulation now owns an occurrence-qualified, generation-checked VHPI graph
    and a bounded VHDL snapshot covering ordered scopes, typed declarations,
    alias/external-name-derived state, live values, drivers, active/postponed
    processes, source identity, PSL attempts, named outcomes, and coverage. The
    debugger exposes `vhdl summary|scopes|objects|processes|psl|all`; VCD signal
    activity, callback ordering, and normalized snapshots agree across
    interpreter, debug, LLVM O0/O2, and cold/warm cache. Record, payload, and
    formatted-byte ceilings fail transactionally. Foreign handles reject, and
    released/recreated handles become stale. Nested generic-subprogram paths
    publish parent regions and coalesce only exact canonical duplicate objects.

    The focused runtime/debugger/expression/advanced-type/projected-waveform/
    PSL/typed-boundary slice passes 7/7 in 29.61 seconds. The warning-clean
    90-step eight-worker full build completes in 2:36.35 at 4,231,040 KiB peak
    RSS with zero swaps, followed by a warning-clean 13-step relink in 28.36
    seconds at 4,230,576 KiB. The complete VHDL label passes 34/34 in 35.64
    seconds at 195,172 KiB peak RSS with zero swaps. WebKit formatting and
    whitespace checks are clean. Preserve Changes 1-15 and begin Change 16's
    durable artifact/checkpoint/cache/relocation/replay state without commit,
    push, sanitizer, or hosted-CI inspection before Change 20.
39. Batch 163 Change 16 is complete in the intentionally dirty worktree.
    Owning-unit schema 17 preserves frontend PSL through `.fsimobj` and
    `.fsimlib`; standalone designs now carry a checksummed schema-1 `FSIMVHIR`
    payload for the complete owning VHDL declaration/type/expression/process
    HIR and analyzed PSL state. Publication/load validates enum ranges, unique
    record identities, every semantic-ID link, schema/checksum/trailing bytes,
    and semantic/DesignIR/runtime consistency before returning a project. The
    IEEE standard-library/cache identity is
    `ieee-1076-2019-16a01232-vhdl-psl-wide-v2`.

    The full PSL fixture now compiles through the non-project object phase,
    exports and relocates a mapped library, elaborates and relocates a
    standalone design, then replays after its producer source moves away.
    Direct, mapped, and design-artifact interpreter/LLVM O2 cold/warm attempts,
    coverage, normalized debugger snapshots, and VCD match exactly. Portable
    VHPI checkpoint restore verifies artifact/cache identity and remaps every
    exported stable object name to a fresh generation-qualified handle.
    Corrupt, truncated, future-schema, trailing, and out-of-range semantic-ID
    payloads reject transactionally. The focused application/artifact/library/
    PSL slice passes 5/5 in 2.47 seconds at 195,172 KiB peak RSS with zero
    swaps. After repairing the VITAL fixture's explicit copied-payload list,
    the complete VHDL label passes 34/34 in 36.21 seconds at 195,068 KiB peak
    RSS with zero swaps; catalog, source-budget, and gap-inventory contracts
    pass 3/3. WebKit formatting for the new/touched PSL sources and whitespace
    checks are clean. Preserve Changes 1-16 and begin Change 17's complete
    clause-indexed conformance inventory without commit, push, sanitizer, or
    hosted-CI inspection before Change 20.
40. Batch 163 Change 17 is complete in the intentionally dirty worktree. The
    authoritative 33-row clause inventory now contains 29 supported IEEE
    1076-2008 and embedded-PSL rows, zero unresolved active rows, and four
    exact Batch 170/post-v2 deferrals. Every supported row names existing
    independent positive, negative, and execution evidence plus parser,
    analyzer, elaboration, runtime, diagnostic, and resource owners. Its
    registered contract rejects active unsupported rows, non-test witnesses,
    duplicate or missing Change 2-16 closure identities, missing IEEE clause
    baselines, misplaced owners, and failure escapes.

    The complete frontend, elaboration, runtime, artifact/library, VHDL, PSL,
    and typed-boundary evidence slice passes 35/35 in 39.38 seconds at 195,020
    KiB peak RSS with zero swaps. Preserve Changes 1-17 and begin Change 18's
    governed cross-engine, cache, debugger, trace, relocation, replay, mixed,
    platform, and resource matrix without commit, push, sanitizer, or hosted-
    CI inspection before Change 20.
41. Batch 163 Change 18 is complete in the intentionally dirty worktree. The
    governed PSL application now executes 17 named direct, interpreter, LLVM
    O0/O2, cold/warm cache, debugger, VCD, object/library/design artifact,
    relocation, replay, checkpoint, multiple-root, SystemVerilog, and SystemC
    stages. POSIX `RLIMIT_AS` and a Windows Job Object enforce a 6 GiB process
    address-space ceiling; delta work remains capped at 1,000, trace signals at
    64, and the CTest at 1,200 seconds. The staged installed-public contract
    now executes `fsim-vhdl --help` and verifies the alias directly.

    The authoritative 44-row release-closure matrix and registered aggregate
    audit freeze 29 supported rows, four deferrals, 87 witnesses, 19 PSL
    diagnostics in the 2,138-code catalog, 835 bounded sources, 316 authored
    test/control files, malformed/race/cancellation/nonconvergence/resource
    behavior, platform/public/artifact provenance, and zero unresolved rows.
    Refreshed graph review reports maximum cognitive complexity 20, loop depth
    1, no recursion, and no scan-in-loop flag. The direct runner passes in 1.71
    seconds at 195,272 KiB peak RSS with zero swaps. The final 13-test closure,
    installed-public, platform, catalog, source, frontend, runtime, and PSL
    slice passes in 3.81 seconds at 195,064 KiB with zero swaps. A stale Windows
    ABI check was corrected to the C probe's append-only 592-byte runtime table
    with its old 560-byte tail and new callbacks at offsets 568/576/584; the
    strict C probe and Windows contract pass 3/3. Preserve Changes 1-18 and
    begin Change 19 documentation/count/digest synchronization without commit,
    push, sanitizer, or hosted-CI inspection before Change 20.
42. Batch 163 Change 19 is complete in the intentionally dirty worktree. New
    public VHDL/PSL support, producer-independent tutorial, and closure-audit
    documents are synchronized with README, language support, architecture,
    VHPI, diagnostics, feature/evidence matrices, UVM/current release audits,
    and the installed Markdown surface. The installed contract executes
    `fsim-vhdl --help`; the documentation contract freezes both VHDL/PSL
    inventory digests, the 29/0/4 boundary, 17 governed stages, pass criteria,
    platform/resource limits, graph complexity, and exact ABI offsets.

    Live inventories are 2,138 diagnostics, 835 bounded C/C++ sources, 955
    SPDX-owned files including `.clang-format`, 316 authored test/control
    files, 1,279 executable rows, 5,116 evidence cells, 604 paths split
    265/312/27 test/production/release, 138 runtime files, and 36 corpus CTests.
    Exact digests are feature matrix
    `e38217f0ced26556c13e4ef6d4f32aa4d46172192ec54e926e739d165abbbab7`,
    evidence
    `2d9a2bf8fdc360b6b484087848eeb00def2a61cff5fecfacf9400fadcec2010b`,
    gap inventory
    `0a60ca24775a1e5e7282059b282557d36656e1f926d4207f1f3df48f4c8b891b`,
    and closure matrix
    `169bc75fd1d0713fcc5760da8a22fe12376f0e655b26510566e33d78a336de35`.
    The complete non-governed-UVM CMake documentation/inventory/release slice
    passes 34/34 in 20.25 seconds at 60,644 KiB peak RSS with zero swaps.
    WebKit formatting and whitespace checks are clean. Preserve Changes 1-19
    and begin Change 20's fresh clean-first Debug/Release builds, complete
    regressions, governed VHDL/PSL/mixed matrices, and release-candidate gates.
    Do not commit or push until every local Change 20 gate is clean; do not run
    sanitizer or inspect hosted CI at this boundary.
43. Batch 163 Change 20 is complete. Fresh clean-first exact-LLVM 22.1.8 Debug
    and Release eight-worker builds complete 741 steps warning-free in 10:19.03
    and 8:39.05 at 4,286,220 and 3,017,072 KiB peak RSS with zero swaps. Full
    Debug and Release regressions pass 126/126 in 6:16.02 and 5:28.32 at
    3,785,680 and 3,766,280 KiB peak RSS with zero swaps; explicit VHDL-labelled
    matrices pass 36/36 in 37.77 and 35.80 seconds. The first Debug regression
    correctly caught a stored-driver API regression beneath an unresolved
    signal force. Separating underlying driver reads from VHDL
    `'driving_value` force overlays restores both contracts; focused API,
    runtime, resolution, and projected-waveform evidence and both full
    regressions pass. The governed VHDL/PSL, arbitrary-width IEEE numeric/fixed,
    mixed-language, artifact, installed/public, platform/portability, and
    release-candidate gates are all green. New Batch 163 sources and the exact
    Change 20 correction ranges pass the WebKit formatting gate, and the
    repository whitespace check is clean; legacy files are not bulk-reformatted
    here. No sanitizer or hosted-CI inspection ran. Commit and push
    this sole Batch 163 implementation changeset, then create and push the
    documentation-only Batch 164 restart checkpoint and clear context before
    any Batch 164 implementation.

## Batch 162 planned restart checkpoint - 2026-08-09

1. Start in `/home/colin/projects/fsim`, read this file and the authoritative
   expanded Batch 162 allocation in `implementation_plan_v2.md`, and verify
   branch `codex/v2` is clean and synchronized at Batch 161 implementation
   `a8417d17d1dec11945ff5a153dfcc18ea2f6b0d6` plus this documentation-only
   Batch 162 planning checkpoint.
2. This checkpoint implements the required pre-batch flow. Batch 162 is
   expanded into exactly twenty changes without broadening the locked UVM 1.2
   and UVM 2020-3.1 conformance-closure scope. No Batch 162 implementation file
   has changed; clear context after pushing this plan and resume only from this
   section and the authoritative allocation.
3. Preserve complete Batch 161 sequence/sequencer lifecycle and arbitration,
   locks/grabs/responses/macros, driver handshakes, active/passive roles,
   virtual sequences, callbacks/transactions, register hierarchy/values/maps,
   frontdoors/backdoors, standard register sequences, callback/coverage,
   debugger/activity/foreign/checkpoint integration, and exact two-release
   sequence/register environments at pushed commit `a8417d1`.
4. Batch 161's clean-first exact-LLVM 22.1.8 Debug and Release eight-worker
   builds complete 721 steps warning-free in 9:05.64 and 7:52.60; complete
   regressions pass 118/118 in 384.37 and 323.00 seconds. Exact UVM 1.2 and UVM
   2020-3.1 pass 2/2 in 1,033.92 seconds. All fourteen traces are 17,833 bytes
   with SHA-256
   `95caf4dbb04fa7f1b1397df9b40e03a1fdbc19b90c6387221a8b59635ebd5eff`.
5. The final Batch 161 build/test peaks are 4,035,212 KiB for the Debug build,
   2,485,712 KiB for the Release build, 3,779,552/3,787,920 KiB for their full
   regressions, 3,982,708 KiB for the governed-UVM refresh, and 4,775,752 KiB
   for the exact two-release run. Preserve serial exact-source execution and
   explicit ceilings; the former over-20-GiB fsim-sv behavior must not return.
6. Changes 1-4 close printer/comparer, copier, packer/recorder/transaction, and
   event/barrier/pool/queue/heartbeat/spell/policy behavior with deterministic
   two-release execution, ownership, rollback, diagnostics, and limits.
7. Changes 5-8 close command-line/plusarg/test selection, topology/timeout/seed,
   reporting/objection tracing, and factory/config/resource tracing. Changes
   9-12 close exact UVM 1.2 legacy macros/APIs, UVM 2020-3.1 additions and
   deprecations, release differences, version selection, provenance, and
   transactional mixed-release rejection.
8. Changes 13-16 run governed standard and project-owned smoke suites across
   policies/factory/config/report, phase/sequence/TLM/callback/transaction,
   register/coverage/DPI/VPI/VHPI, direct/object/design, interpreter/LLVM O0/O2,
   cache/debug/trace, roots, relocation, replay, and platform contracts.
9. Changes 17-19 execute and close the full conformance gap inventory, require
   zero unresolved supported gaps, consolidate negative/resource/race/memory/
   source/provenance evidence, synchronize every public/release contract, and
   publish the producer-independent tutorial. Change 20 owns all final local
   gates and the sole implementation commit/push.
10. Accumulate Changes 1-20 in one dirty Batch 162 worktree. Use at least eight
    workers for local builds, retain exact test/memory/trace output, update this
    handoff after every completed change, and do not reset, commit, or push the
    implementation before Change 20.
11. Batch 162 is not a sanitizer or hosted-CI monitoring boundary. Do not run a
    sanitizer or inspect hosted CI unless a new failure requires it. Change 20
    runs fresh full non-sanitized exact-LLVM Debug and Release builds, all
    regressions, governed conformance matrices, audits, installed/public/
    platform contracts, and release gates before the single implementation
    commit/push.
12. Before Batch 163 implementation, repeat this flow: expand and save its exact
    restart plan, commit and push the documentation-only checkpoint, then clear
    context. Do not begin Batch 162 Change 1 until this checkpoint itself is
    committed, pushed, and followed by a context clear.
13. Next action after that clear: implement Batch 162 Change 1 only, beginning
    with deterministic UVM printer line/tree/table policies and comparer knobs,
    field/object/array traversal, cycle handling, mismatch accounting, stable
    formatting, resource ceilings, cataloged malformed-policy diagnostics, and
    focused unmodified UVM 1.2/2020.3.1 evidence.
14. Change 1 is complete in the intentionally dirty Batch 162 worktree. The
    simulation-owned object service now owns deterministic escaped line, tree,
    and table formatting with configurable indentation, separators, columns,
    type/size visibility, and output ceilings. Detailed comparison implements
    deep, shallow, and reference recursion; type, physical, and abstract knobs;
    per-field recursion overrides; complete mismatch counting with bounded
    retained detail; and bijective alias/cycle handling across scalar, string,
    object, sequential-array, and associative-array fields. Legacy Boolean
    compare and raw print-entry APIs preserve compatible defaults. Conflicting
    field flags and malformed printer/comparer settings are cataloged as
    `FSIM-UVM-POLICY-001`.
15. Runtime proof covers all recursion modes, scalar/string/object/array/keyed
    traversal, cycles and aliases, physical/abstract/type filtering, field
    overrides, complete bounded mismatch accounting, stable repeated output,
    escaped control/quote/backslash text, formatting ceilings, and every
    malformed-policy path. The runtime, diagnostics-catalog, source-line-budget,
    and UVM phase/TLM matrix gates pass 4/4. The final exact-LLVM governed build
    refreshes 88 affected steps warning-free. Exact unmodified UVM 1.2 passes
    in 469.72 seconds and UVM 2020-3.1 passes in 581.15 seconds, 2/2 in 1,050.88
    seconds with maximum RSS 4,775,892 KiB and zero swaps. All twenty release/
    stage transcripts retain the policy marker. All fourteen traces are 17,833
    bytes with SHA-256
    `95caf4dbb04fa7f1b1397df9b40e03a1fdbc19b90c6387221a8b59635ebd5eff`.
16. Preserve all Change 1 tracked and untracked files. Do not reset, commit, or
    push before Change 20, and do not run sanitizer or hosted-CI gates at this
    non-boundary. Next implement Batch 162 Change 2: deep/shallow/reference
    copier behavior, automation hooks, field macros, clone/copy ownership,
    recursion/alias preservation, rollback, exception containment, stale and
    cross-owner rejection, explicit ceilings, cataloged diagnostics, and exact
    two-release evidence.
17. Change 2 is complete in the intentionally dirty Batch 162 worktree. UVM
    object references are service-owner branded, and compatible raw-handle
    clone/copy APIs delegate to one detailed copier. Deep copies preserve cycles
    and bijective aliases across object, vector, and keyed container fields;
    shallow copies materialize one object level; reference copies retain source
    handles. Physical/abstract filters and per-field deep/shallow/reference/no-
    copy automation flags override the global policy. Runtime packed-value
    widths are copied from the source only after exact-specialization and
    property-kind validation. Virtual creation and `do_copy` hooks execute per
    object. Any callback, stale/foreign ownership, type/shape, or resource
    failure rolls back destination state and every newly created object under
    cataloged `FSIM-UVM-COPY-001` through `005` diagnostics.
18. Focused runtime proof covers scalar/string/object/vector/keyed fields,
    cycles, aliases, deep/shallow/reference modes, per-field overrides,
    physical/abstract filtering, no-copy, current-width packed values, self-
    copy, hook counts and failure containment, stale/foreign/type rejection,
    clone-root and recursive ownership, and resource rollback. Runtime,
    diagnostics-catalog, source-line-budget, and UVM phase/TLM matrix gates pass
    4/4. The governed exact-LLVM tree refreshes 80 affected steps warning-free
    and rebuilds three steps after the final runtime-width correction. Exact
    UVM 1.2 passes in 469.92 seconds and UVM 2020-3.1 passes in 587.70 seconds,
    2/2 in 1,057.62 seconds with maximum RSS 4,775,736 KiB and zero swaps. All
    twenty stage transcripts retain the copier marker. All fourteen traces are
    17,833 bytes with SHA-256
    `95caf4dbb04fa7f1b1397df9b40e03a1fdbc19b90c6387221a8b59635ebd5eff`.
19. Preserve all Changes 1-2 tracked and untracked files. Do not reset, commit,
    or push before Change 20; do not run sanitizer or hosted-CI gates at this
    non-boundary. Next implement Batch 162 Change 3: scalar/string/real/object/
    array packer and recorder policies, endian and bit/byte/int packing, unpack
    validation, transaction attribute/link integration, deterministic replay,
    retained-record limits, rollback, diagnostics, and exact two-release proof.
20. Change 3 is complete in the intentionally dirty Batch 162 worktree. The
    versioned typed packer closes big/little-endian four-/nine-state bits,
    bytes, integers, embedded-NUL strings, reals, object identities, recursive
    arrays, optional metadata, strict unpack validation, and bounded malformed/
    resource diagnostics. Transaction object recording publishes prefixed
    typed automation attributes transactionally; trace replay validates and
    reconstructs ordered begin/attribute/link/end identity, hierarchy, timing,
    links, attributes, and terminal state under retained-record ceilings.
    Runtime, diagnostics-catalog, source-line-budget, and UVM phase/TLM matrix
    gates pass 4/4. The governed exact-LLVM tree refreshes 100 affected steps
    warning-free and rebuilds three steps after the exact inherited-field count
    correction. Exact UVM 1.2 passes in 459.60 seconds and UVM 2020-3.1 passes
    in 567.53 seconds, 2/2 in 1,027.12 seconds with maximum RSS 4,775,616 KiB
    and zero swaps. All twenty stage transcripts retain the packer/recorder
    marker. All fourteen traces are 27,913 bytes with SHA-256
    `63a73aa8584815862ee48c591dee8c0b5a164a9f0aa43234a8628ef302019409`.
21. Preserve all Changes 1-3 tracked and untracked files. Do not reset, commit,
    or push before Change 20; do not run sanitizer or hosted-CI gates at this
    non-boundary. Next implement Batch 162 Change 4: event, event-pool, barrier,
    pool, queue, heartbeat, spell-challenge, and remaining policy-class
    behavior with callback/wait/threshold/reset/cancellation semantics,
    deterministic lifecycle order, bounded resources, and exact two-release
    evidence.
22. Change 4 is complete in the intentionally dirty Batch 162 worktree. The
    simulation-owned synchronization service closes named event-pool identity,
    trigger/persistent/on/off waits, object payloads, callback snapshots and
    containment, scheduler timing, reset/wakeup/cancel, threshold/auto-reset
    barriers, typed deterministic pools/queues, event-driven ANY/ALL/ONE
    heartbeat windows, bounded spell challenges, referenced teardown, and
    waiter-cancelling service reset. Cataloged `FSIM-UVM-SYNC-001` and `002`
    diagnostics cover stale, malformed, state, ownership, threshold, mode,
    index, text, entry, participant, work, and mutation failures. Runtime,
    diagnostics-catalog, source-line-budget, and UVM phase/TLM matrix gates pass
    4/4. The exact-LLVM tree refreshes 92 affected steps warning-free. Exact UVM
    1.2 passes in 463.93 seconds and UVM 2020-3.1 passes in 567.65 seconds, 2/2
    in 1,031.57 seconds with maximum RSS 4,775,812 KiB and zero swaps. All
    twenty stage transcripts retain the synchronization marker. All fourteen
    traces remain 27,913 bytes with SHA-256
    `63a73aa8584815862ee48c591dee8c0b5a164a9f0aa43234a8628ef302019409`.
23. Preserve all Changes 1-4 tracked and untracked files. Do not reset, commit,
    or push before Change 20; do not run sanitizer or hosted-CI gates at this
    non-boundary. Next implement Batch 162 Change 5: complete command-line
    processor ordered argv retention, exact/prefix queries, value extraction,
    plusargs, tool/version arguments, duplicates, malformed input, simulation
    isolation, bounded query/text resources, diagnostics, and exact two-release
    evidence.
24. Change 5 is complete in the intentionally dirty Batch 162 worktree. The
    simulation-owned command-line processor now closes ordered argv, plusarg
    and UVM subsets, explicit exact/prefix matches, first/all value extraction,
    ordered duplicates, stable tool/version identity, simulation isolation,
    missing/malformed queries, and independent query text/result-count/result-
    byte ceilings. Existing transactional factory/config/verbosity/timeout/
    trace-switch application remains intact. Cataloged `FSIM-UVM-CMD-001` and
    `002` diagnostics distinguish malformed/state input from retained argv,
    setting, query, result, and downstream resource exhaustion. Runtime,
    diagnostics-catalog, source-line-budget, and UVM phase/TLM matrix gates pass
    4/4. The exact-LLVM tree refreshes 46 affected steps warning-free. Exact UVM
    1.2 passes in 459.05 seconds and UVM 2020-3.1 passes in 557.18 seconds, 2/2
    in 1,016.23 seconds with maximum RSS 4,775,796 KiB and zero swaps. All
    twenty stage transcripts retain the command-line query marker. All fourteen
    traces remain 27,913 bytes with SHA-256
    `63a73aa8584815862ee48c591dee8c0b5a164a9f0aa43234a8628ef302019409`.
25. Preserve all Changes 1-5 tracked and untracked files. Do not reset, commit,
    or push before Change 20; do not run sanitizer or hosted-CI gates at this
    non-boundary. Next implement Batch 162 Change 6: `run_test`, test-name
    selection, topology printing, global timeout, seed selection/reporting,
    command precedence, repeated-run cleanup, fatal/finish behavior, and
    deterministic interpreter/compiled/debug execution for both governed
    releases.
26. Change 6 is complete in the intentionally dirty Batch 162 worktree. The
    simulation-owned test runner applies explicit-before-command precedence for
    test name, seed, and global timeout; creates `uvm_test_top` through the
    factory; prints deterministic bounded topology; distinguishes completion,
    `$finish`, fatal/exception, and timeout outcomes; rejects stale/reentrant
    runs; and cleans every repeated hierarchy. Exact source proof uses a real
    registered `uvm_test` and retains the runner marker across all twenty
    direct/object/O0/O2/cache/debug stage transcripts. Cataloged
    `FSIM-UVM-RUN-001` through `003` cover lifecycle/selection, resource/
    timeout, and fatal termination.
27. The governed exact-LLVM tree refreshes 93 affected steps warning-free.
    Exact UVM 1.2 passes in 469.08 seconds and UVM 2020-3.1 passes in 577.08
    seconds, 2/2 in 1,046.16 seconds with maximum RSS 4,776,356 KiB and zero
    swaps. All fourteen traces remain 27,913 bytes with SHA-256
    `63a73aa8584815862ee48c591dee8c0b5a164a9f0aa43234a8628ef302019409`.
    Runtime, diagnostics-catalog, source-line-budget, and the 79-family UVM
    phase/TLM matrix gates pass 4/4. Preserve Changes 1-6 without reset,
    commit, push, sanitizer, or hosted CI. Next implement Change 7: close
    report verbosity/severity/action/file switches and objection tracing with
    hierarchical overrides, phase/time-aware output, callback/catcher
    containment, command precedence, bounded resources, and stable exact
    cross-engine transcripts.
28. Change 7 is complete in the intentionally dirty Batch 162 worktree. The
    command-line processor applies default verbosity, first-wins max-quit and
    objection-trace controls initially, then source-ordered component or ID
    verbosity controls at exact phase entry or due simulation time. Sorted
    root/descendant matching, bounded wildcards, and transactional match/work/
    trace/report-capacity preflight preserve deterministic hierarchy behavior.
    Exact proof combines hierarchical severity, action, file, verbosity, and
    catcher policy with stable bounded objection trace enable/disable sequence
    boundaries and escaped phase-aware records. `FSIM-UVM-CMD-001`/`002` and
    `FSIM-UVM-OBJ-003` retain malformed and resource-limit failures.
29. The governed exact-LLVM tree refreshes 68 affected steps warning-free.
    Exact UVM 1.2 passes in 469.61 seconds and UVM 2020-3.1 passes in 584.10
    seconds, 2/2 in 1,053.72 seconds with maximum RSS 4,776,356 KiB and zero
    swaps. All twenty stage transcripts retain the report-control and
    objection-trace markers. All fourteen traces remain 27,913 bytes with
    SHA-256
    `63a73aa8584815862ee48c591dee8c0b5a164a9f0aa43234a8628ef302019409`.
    Runtime, diagnostics-catalog, source-line-budget, and UVM phase/TLM matrix
    gates pass 4/4. Preserve Changes 1-7 without reset, commit, push, sanitizer,
    or hosted CI. Next implement Change 8: factory/config/resource tracing,
    print/usage inventories, wildcard and override reporting, root isolation,
    callback containment, deterministic order, debugger/activity integration,
    and cataloged option/resource diagnostics.
30. Change 8 is complete in the intentionally dirty Batch 162 worktree.
    Simulation-owned factory, resource, and config services now retain bounded,
    stable, fully escaped traces with deterministic oldest-first eviction and
    contained callback failures. Factory traces include create/debug resolution
    and ordered instance/type override steps. Resource traces cover name/type
    lookup, read, and write; config traces cover set/get/exists. Stable factory
    and audited config inventories expose registration, override, creation,
    precedence, and read/write usage. Standard resource/config trace plusargs
    enable shared service state without a later absent switch disabling it.
    The debugger's `uvm configuration` section and immutable configuration
    activity events expose the same bounded interval without cross-root or
    cross-simulation leakage.
31. The exact-LLVM tree refreshes 75 affected steps warning-free and four steps
    after the final control-byte escaping correction. Exact UVM 1.2 passes in
    473.16 seconds and UVM 2020-3.1 passes in 580.81 seconds, 2/2 in 1,053.97
    seconds with maximum RSS 4,776,552 KiB and zero swaps. All twenty stage
    transcripts retain the factory/config/resource/debug/activity marker. All
    fourteen traces are 28,345 bytes with SHA-256
    `62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
    Runtime, diagnostics-catalog, source-line-budget, and UVM phase/TLM matrix
    gates pass 4/4. Preserve Changes 1-8 without reset, commit, push, sanitizer,
    or hosted CI. Next implement Change 9: close UVM 1.2 legacy field/object/
    component/sequence/register macros, callback declarations, factory
    utilities, report helpers, and exact expansion/call signatures against the
    unmodified governed source tree.
32. Change 9 is complete in the intentionally dirty Batch 162 worktree. The
    governed frontend probe expands real UVM 1.2 field/object/component/
    registry, sequence, callback, analysis-implementation, and report macros
    and checks the generated factory utilities and call signatures without
    modifying either source tree. It also verifies the compatible release
    distinction between UVM 1.2 `__m_uvm_field_automation` and UVM 2020.3.1
    `__m_uvm_execute_field_op`. A project-owned macro source compiled beside
    both packages retains generated item/sequence/component/register/callback
    methods, nonempty sequence/callback/report bodies, analysis-imp
    inheritance, factory wrappers, and debug resolution through direct,
    object, design, interpreter, O0/O2, cache, and debugger paths. The exact
    frontend suite passes in 63.58 seconds at 413,868 KiB maximum RSS with zero
    swaps.
33. Exact UVM 1.2 passes in 480.29 seconds and UVM 2020-3.1 passes in 583.37
    seconds, 2/2 in 1,063.67 seconds with maximum RSS 4,804,484 KiB and zero
    swaps. All twenty stage transcripts retain the legacy-macro marker. All
    fourteen traces remain 28,345 bytes with SHA-256
    `62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
    Frontend, runtime, diagnostics-catalog, source-line-budget, and UVM
    phase/TLM matrix gates pass 5/5. Preserve Changes 1-9 without reset, commit,
    push, sanitizer,
    or hosted CI. Next implement Change 10: close remaining UVM 1.2 legacy APIs
    and behavioral contracts across phases, objections, TLM, sequences,
    callbacks, registers, policies, command-line processing, deprecation
    aliases, and negative diagnostics.
34. Change 10 is complete in the intentionally dirty Batch 162 worktree.
    Direct and portable governed UVM 1.2 execution requires 107 exact method
    profiles, including function/task kind and argument count, across phases,
    objections, TLM ports, sequences, callbacks, registers, object/printer/
    comparer/packer/recorder policies, command-line processing, deprecated
    component configuration/stop APIs, and `uvm_test_done_objection`. The
    governed frontend also requires package-level `uvm_top`, `uvm_test_done`,
    stop-request, and timeout aliases; expands deprecated sequence/sequencer
    registration utilities; proves `UVM_NO_DEPRECATED` removes them; and
    retains exact undefined-macro, expansion-depth, and arity diagnostics.
    The exact frontend suite passes in 64.19 seconds at 413,888 KiB maximum RSS
    with zero swaps. Normal and governed affected builds complete five steps
    warning-free with eight workers.
35. Exact UVM 1.2 passes in 458.84 seconds and UVM 2020-3.1 passes in 560.74
    seconds, 2/2 in 1,019.58 seconds with maximum RSS 4,804,092 KiB and zero
    swaps. All twenty stage transcripts retain the legacy-API marker. All
    fourteen traces remain 28,345 bytes with SHA-256
    `62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
    Frontend, runtime, diagnostics-catalog, source-line-budget, and UVM
    phase/TLM matrix gates pass 5/5. Preserve Changes 1-10 without reset,
    commit, push, sanitizer, or hosted CI. Next implement Change 11: close UVM
    2020-3.1 additions, IEEE 1800.2 names/signatures, policy/reporting
    additions, deprecated/removed aliases, version macros, and release-specific
    behavior while preserving the common simulation-owned runtime model.
36. Change 11 is complete in the intentionally dirty Batch 162 worktree.
    Direct and portable governed UVM 2020.3.1 execution requires 134 exact
    method profiles across new policy, field-operation, and copier classes;
    object seeding/long-integer/copy dispatch; printer/comparer/packer/recorder
    accessors and recursion state; report-server summaries; and retained
    component configuration plus test-done compatibility shims. Removed
    component status/kill/stop and sequence-library methods must remain absent
    after every artifact load. The governed frontend requires each release's
    exact version numbers/string/compatibility ladder, the IEEE policy/copier/
    revision names, removed global test-done/stop/timeout aliases, and two
    exact undefined-macro diagnostics for removed sequence/sequencer utilities.
    The exact frontend suite passes in 65.25 seconds at 414,100 KiB maximum RSS
    with zero swaps. Normal and governed affected builds complete five steps
    warning-free with eight workers.
37. Exact UVM 1.2 passes in 457.48 seconds and UVM 2020-3.1 passes in 557.05
    seconds, 2/2 in 1,014.54 seconds with maximum RSS 4,804,040 KiB and zero
    swaps. All twenty stage transcripts retain the UVM-2020 API marker. All
    fourteen traces remain 28,345 bytes with SHA-256
    `62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
    Frontend, runtime, diagnostics-catalog, source-line-budget, and UVM
    phase/TLM matrix gates pass 5/5. Preserve Changes 1-11 without reset,
    commit, push, sanitizer, or hosted CI. Next implement Change 12: explicit
    dual-release selection and normalized compatibility dispatch, retained
    source/release provenance through artifacts/caches/checkpoints/replay,
    transactional mixed-release rejection, and the complete difference matrix.
38. Change 12 is complete in the intentionally dirty Batch 162 worktree.
    Manifest `uvm_release` and CLI `--uvm-release` selection normalize to a
    public 1.2/2020.3.1 enum and complete compatibility-dispatch record. The
    selected release is checked against the real parsed or portable `uvm_pkg`
    surface. `FSIM-UVM-VERSION-001` rejects invalid language placement, mixed
    source/object releases, and selected/artifact disagreement;
    `FSIM-UVM-VERSION-002` rejects an API-surface mismatch. Synthetic focused
    proof exercises both failures before any project/artifact publication.
39. Canonical release plus a content-derived exact source identity now crosses
    source settings, checked/built projects, `.fsimobj` metadata and class
    payloads, project/specialization cache keys, `.fsimdesign` metadata, and
    checkpoint restore/replay. Object format 2, owning-unit schema 14, design
    format 4, UVM-state schema 2, and checkpoint schema 2 freeze that boundary.
    The normal exact-LLVM tree rebuilds 183 affected steps and the governed
    tree 63 steps warning-free with eight workers. Project/library/object/
    design/cache/diagnostics/matrix/source gates pass 8/8; application/
    artifact/runtime gates pass 3/3; and the standalone negative proof passes.
40. Direct UVM 1.2 passes in 1:50.48 at 4,296,956 KiB maximum RSS and direct
    UVM 2020.3.1 in 2:04.50 at 4,805,272 KiB, both with zero swaps. The full
    serial matrices pass 10/10 in 7:55.26 and 10/10 in 9:27.80, respectively:
    20/20 in 17:23.06 with matrix maximum RSS 4,803,608 KiB and zero swaps.
    All fourteen traces are 28,345 bytes with SHA-256
    `62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
    Preserve Changes 1-12 without reset, commit, push, sanitizer, or hosted CI.
    Next implement Change 13: run and retain the governed standard and
    project-owned object-policy, factory/config/resource, command-line,
    reporting, callback, test-selection, topology, timeout, and seed smoke
    suites through direct source and portable objects for both releases.
41. Change 13 is complete in the intentionally dirty Batch 162 worktree. One
    project-owned source derives `uvm_test`, binds the governed object-policy,
    factory, resource/configuration, command-line, report-server, and callback
    types, and executes eleven named core smoke methods. Direct and portable
    application stages require its standard base and every nonempty method body
    after object/design reload, then exercise the matching simulation-owned
    policies, factory, databases, command-line/report/callback controls, and
    test selection/topology/timeout/seed behavior. The static source harness
    freezes the same eleven-category inventory and exact transcript marker.
42. The new 2020.3.1 report-server member exposed and closed a qualified
    package-class lookup gap. External package qualification now recognizes a
    class only when directly declared or explicitly exported and retains its
    package dependency; a focused class-member regression passes. Normal and
    governed exact-LLVM Debug trees refresh 21 and 159 steps warning-clean with
    eight workers. Elaboration, diagnostics catalog, source policy, source
    harness, and phase/TLM matrix gates pass 5/5.
43. Direct UVM 1.2/2020.3.1 passes in 1:50.06/2:10.02 at
    4,299,260/4,861,476 KiB maximum RSS. Portable compile passes in
    2:29.65/3:17.21 at 3,365,800/3,735,852 KiB; object-only O2 elaboration in
    1:38.54/2:00.99 at 3,239,364/3,746,852 KiB; and loaded execution in
    1.50/1.69 seconds at 936,660/1,041,144 KiB. All stages report zero swaps.
    The four direct/portable traces are 28,345 bytes with SHA-256
    `62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
    Preserve Changes 1-13 without reset, commit, push, sanitizer, or hosted CI.
    Next implement Change 14's phase/objection/sequence/role/TLM/callback/
    transaction/cancellation smoke matrix across engines, caches, roots, debug,
    and replay.
44. Change 14 is complete in the intentionally dirty Batch 162 worktree. The
    existing project phase/TLM source now has explicit post-load assertions for
    32 flow contracts: nine phase callback bodies, two TLM1 members, seven
    sequence/role/callback base identities, and fourteen common TLM2 payload
    profiles. Every stage checks those retained profiles before exercising
    phase/objection scheduling, sequence/sequencer/driver/monitor/agent and
    virtual-sequence behavior, TLM1/TLM2, callbacks, transactions,
    cancellation, multiple roots, and checkpoint replay. The static source
    harness freezes ten matching flow types and the transcript freezes one
    flow-smoke marker.
45. The serial exact matrix passes UVM 1.2 ten stages in 7:53.85 at
    4,299,156 KiB maximum RSS and UVM 2020-3.1 ten stages in 9:46.97 at
    4,861,116 KiB: 20/20 stages in 17:40.82 with zero swaps. Both retained
    stage logs contain exactly ten flow-smoke transcripts. All fourteen traces
    are 28,345 bytes with SHA-256
    `62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
46. The normal exact-LLVM Debug tree refreshes three affected steps warning-
    clean with eight workers and the governed tree is already current.
    Preserve Changes 1-14 without reset, commit, push, sanitizer, or hosted CI.
    Next implement Change 15's register block/map/field/memory, adapter/
    predictor/frontdoor/backdoor, standard sequence, callback/coverage, DPI,
    VPI, and VHPI smoke suites with relocation, mixed-abstraction, negative,
    checkpoint, and retained-cap evidence.
47. Change 15 is complete in the intentionally dirty Batch 162 worktree. Every
    direct or loaded-design stage validates six project register-role base
    identities, adapter profiles, and stable governed field/map/memory method
    profiles before execution. The exact environment proves hierarchy,
    little/big endian, sparse byte enables, adapter/frontdoor/predictor,
    mixed-VPI/VHPI backdoor, standard access sequence, callback/coverage, DPI
    callback, wrong-width rollback, debugger inventory, relocation, checkpoint
    replay, and a one-record-short retained capture cap. The transcript freezes
    the complete register-smoke contract.
48. The serial exact matrix passes UVM 1.2 ten stages in 7:59.08 at
    4,299,536 KiB maximum RSS and UVM 2020-3.1 ten stages in 9:55.15 at
    4,860,764 KiB: 20/20 stages in 17:54.23 with zero swaps. Both retained logs
    contain exactly ten register-smoke transcripts. All fourteen traces are
    28,345 bytes with SHA-256
    `62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
49. The normal and governed exact-LLVM Debug targets each refresh three
    affected steps warning-clean with eight workers. Diagnostics catalog,
    source-line-budget, UVM source-harness, and phase/TLM matrix pass 4/4.
    One exploratory broader `fsim.v1-inventory-release` run remains expected-
    red because its frozen release text does not yet match the accumulated
    2,054-code diagnostic catalog; Changes 18-20 own that synchronization.
    Preserve Changes 1-15 without reset, commit, push, sanitizer, or hosted CI.
    Next implement Change 16's Linux/Windows source and ABI portability
    contracts plus the bounded combined direct/object/design, interpreter,
    LLVM O0/O2, cache, debug, trace, foreign, relocation, and replay matrix.
50. Change 16 is complete in the intentionally dirty Batch 162 worktree. The
    exact test child applies a 6 GiB POSIX `RLIMIT_AS` or Windows Job Object
    process-memory ceiling before governed parsing. Each direct, compile,
    elaborate, and simulate invocation has a validated 1,200-second CMake
    timeout; each serial release matrix has the approved 7,200-second CTest
    timeout. The C11 probe freezes x64 snapshot/record/activity/host sizes and
    offsets, append-only host tail, and callback calling convention. The fast
    platform contract freezes both source registrations, filesystem-neutral
    paths, MSVC setup, Windows `__cdecl`, limits, and transcript marker.
51. The capped matrix passes UVM 1.2 ten stages in 7:55.05 at 4,299,868 KiB
    maximum RSS and UVM 2020-3.1 ten stages in 9:55.48 at 4,861,176 KiB: 20/20
    in 17:50.53 with zero swaps and twenty platform-contract markers. Focused
    capped direct probes pass at 4,299,428/4,861,616 KiB. All fourteen traces
    are 28,345 bytes with SHA-256
    `62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
52. The normal target refreshes two final limiter steps and the governed tree
    refreshes five affected steps warning-clean with eight workers. Runtime,
    UVM platform/source/matrix, MSVC Debug/Release, Windows LLVM, resource/tool
    portability, installed-public, diagnostic-catalog, and source-line-budget
    gates pass 12/12. Preserve Changes 1-16 without reset, commit, push,
    sanitizer, or hosted CI. Next execute Change 17's complete governed
    standard/project conformance inventory, classify every observed mismatch,
    and close every in-scope gap with no allowlisted supported failure.
53. Change 17 is complete in the intentionally dirty Batch 162 worktree. The
    authoritative 18-row tab-separated inventory selects exactly 17 supported
    families per release and assigns positive, negative, and execution owners
    to every row. The post-load verifier requires unique rows/classes, valid
    evidence paths, 53 UVM 1.2 or 56 UVM 2020-3.1 governed class identities,
    and all 27 project identities after direct builds and portable design
    reloads. The registered fast contract rejects changed schema/counts and
    every failure-waiver spelling. All mismatches are therefore supported hard
    failures; the complete inventory observed none and left zero in-scope gaps.
54. Focused direct inventory probes pass in 1:52.41/2:09.16 at
    4,299,032/4,860,876 KiB maximum RSS. The exact UVM 1.2 matrix passes ten
    stages in 8:03.14 at 4,299,292 KiB and UVM 2020-3.1 ten stages in 9:51.85
    at 4,861,336 KiB: 20/20 in 17:54.99 with zero swaps and ten exact inventory
    markers per release. All fourteen traces are 28,345 bytes with SHA-256
    `62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
55. The normal and governed exact-LLVM Debug targets each rebuild three steps
    warning-clean with eight workers. Runtime, diagnostics-catalog,
    source-line-budget, UVM source-harness, platform, phase/TLM matrix, and
    conformance-inventory gates pass 7/7. Preserve Changes 1-17 without reset,
    commit, push, sanitizer, or hosted CI. Next implement Change 18's complete
    two-release compatibility/behavior matrix, diagnostic/source/complexity/
    resource/provenance consolidation, and explicit zero-unresolved-supported-
    gap audit.
56. Change 18 is complete in the intentionally dirty Batch 162 worktree. The
    authoritative 21-row release closure matrix freezes canonical releases,
    all nine public compatibility switches, 53/56 governed and 27 project
    classes, 17 active families, ten stages, memory/trace baselines,
    artifact/cache provenance, and zero unresolved supported gaps. Its
    post-load verifier requires every exact value and evidence owner.
57. The aggregate closure contract composes the diagnostic, source, SPDX,
    conformance, and release-inventory audits. It freezes 82 `FSIM-UVM-*`
    diagnostics within the 2,071-code catalog, 826 bounded C/C++ sources, 937
    SPDX-owned files, 313 authored test/control files, and the reviewed bounded
    compare/copy/replay/inventory complexity. The stale release inventory was
    corrected to those current counts. Race, cancellation, stale, cross-owner,
    resource, rollback, artifact, cache, and trace owners are explicit and no
    supported mismatch or waiver remains.
58. Direct closure probes pass in 1:48.68/2:24.74 at
    4,299,044/4,860,820 KiB maximum RSS. The exact UVM 1.2 matrix passes ten
    stages in 8:28.42 at 4,299,288 KiB and UVM 2020-3.1 passes ten in 11:14.87
    at 4,860,288 KiB: 20/20 in 19:43.29 with zero swaps and ten closure markers
    per release. All fourteen traces are 28,345 bytes with SHA-256
    `62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
    The normal/governed eight-worker builds are warning-clean and the focused
    closure suite passes 9/9. Preserve Changes 1-18 without reset, commit,
    push, sanitizer, or hosted CI. Next implement Change 19's complete public
    documentation, tutorial, evidence, inventory, portability, and release-
    contract synchronization.
59. Change 19 is complete in the intentionally dirty Batch 162 worktree.
    README, the UVM/version guide, architecture, language support, diagnostics,
    feature/evidence matrices, source provenance, resource baselines, public/
    inventory/differential/SystemVerilog/release audits, and this handoff now
    describe the same bounded two-release closure. The installed producer-
    independent tutorial covers source sets/manifests, standard UVM code,
    interpreter/compiled/debug engines, cache/trace comparison, portable
    phases, plusargs, resource limits, pass criteria, and diagnosis without a
    producer wrapper or patched source. The new documentation gate freezes 15
    synchronized owners and installed Markdown.
60. The reviewed release baseline advances to 1,279 executable rows, 5,116
    evidence cells, and 604 exact paths split across 265 test, 312 production,
    and 27 release/build owners, retaining 138 runtime files and 36 corpus
    CTests. Matrix digest is
    `70deacfc60402730e5a567494e74aa015b4759b3655983a2ff6464c97ef11d42`;
    evidence digest is
    `2d9a2bf8fdc360b6b484087848eeb00def2a61cff5fecfacf9400fadcec2010b`.
    Authored ownership is 937 SPDX files; diagnostics remain 2,071, bounded
    C/C++ sources 826, and authored test/control files 313.
61. Both configured exact-LLVM Debug trees regenerate warning-clean with eight
    workers and no compilation. Documentation, installed-public, portability,
    inventory, and release contracts pass 32/32 in 8.98 seconds, including the
    Unicode staged install with the tutorial and closure audit. Preserve
    Changes 1-19 without reset, commit, push, sanitizer, or hosted CI. Next run
    Change 20's fresh full Debug/Release builds, all regressions, both governed
    matrices, audits, installed/public/platform contracts, and release gates;
    only after every local gate is clean commit and push the accumulated batch.
62. Change 20 and Batch 162 are complete in the accumulated implementation
    worktree. Fresh clean-first exact-LLVM 22.1.8 Debug and Release eight-worker
    builds each complete 733 steps warning-free in 9:36.84 and 8:49.59 with
    peak RSS 4,070,136 and 2,506,320 KiB and zero swaps. Their complete
    regressions pass 122/122 in 6:24.90 and 5:26.29 with peak RSS 3,777,748 and
    3,783,548 KiB and zero swaps.
63. The governed tree is current. Exact UVM 1.2 passes all ten stages in
    8:18.93 and UVM 2020-3.1 in 10:20.25: 2/2 in 18:39.19 with peak RSS
    4,860,744 KiB and zero swaps. All fourteen regenerated VCD/FST files are
    28,345 bytes with SHA-256
    `62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
64. The full regressions include every diagnostic/source/license/inventory,
    documentation, installed/public/platform/portability, evidence, and
    release-candidate gate. No sanitizer or hosted-CI inspection ran at this
    non-monitoring boundary. The final post-documentation contract slice passes
    32/32 and the whitespace check is clean. Commit and push the single
    accumulated Changes 1-20 implementation, then save and push the
    Batch 163 restart plan and clear context before Batch 163 implementation.

## Batch 161 planned restart checkpoint - 2026-08-09

1. Start in `/home/colin/projects/fsim`, read this file and the authoritative
   expanded Batch 161 allocation in `implementation_plan_v2.md`, and verify
   branch `codex/v2` is clean and synchronized at Batch 160 implementation
   `2b5810303c7f8e39f9846955d95871ada5bfcd2c`, the complete hosted portability
   and correctness repair chain through
   `2158b2cd484b9b06f66621d2d3dca5eabb9cb322`, and this Batch 161 planning and
   CI-budget checkpoint.
2. This checkpoint implements the required pre-batch flow. Batch 161 is
   expanded into twenty exact changes without broadening the locked UVM
   sequence, callback, transaction-recording, and register-model scope. No
   Batch 161 implementation file has changed; clear context after pushing this
   plan and resume only from this section and the authoritative allocation.
3. Preserve the complete Batch 160 phase, objection, TLM1/TLM2, debugger,
   activity, foreign, artifact, checkpoint, and exact-UVM implementation. Its
   final sanitizer passes 117/117 in 1,180.07 seconds; fresh exact-LLVM 22.1.8
   Debug and Release pass 117/117 in 381.93 and 314.14 seconds; and the governed
   exact UVM 1.2/2020-3.1 tests pass 2/2 in 856.23 seconds. All fourteen final
   traces are 10,357 bytes with SHA-256
   `f78d9f125531a9d4764e6c5336e0bfdf8d9893577932f1cbb0d198355fb7c7ac`.
   Hosted run `31304022606` passes ten of eleven jobs. Its sole non-green job
   passes tests 1-75 of 118 before the 70-minute Windows MSVC/LLVM Debug job
   ceiling cancels it during `fsim.application.sv_containers`, with no test
   failure in the log. The user explicitly accepted that result on 2026-08-09,
   directed raising the Windows LLVM ceiling to 120 minutes, and directed
   advancement to Batch 161.
4. Changes 1-4 define simulation-owned sequence-item, sequence, and sequencer
   identities and lifecycle; execute exact sequence callbacks; implement every
   standard deterministic arbitration mode; and complete locks, grabs,
   responses, macros, and constraint-randomization integration with bounded
   negative evidence.
5. Changes 5-8 implement driver/sequencer pull and push handshakes; integrate
   drivers, monitors, agents, subscribers, and scoreboards; execute virtual
   sequencers/sequences across child domains; and complete callbacks plus
   engine-neutral transaction recording.
6. Changes 9-12 define register-model block/map/register/field/memory identity
   and hierarchy; implement rights, reset, desired/mirror/predict state; own
   hierarchical maps, addressing, byte enables, endianness, memories, and
   multiple maps; and connect adapters, predictors, and frontdoor operations.
7. Changes 13-16 implement user frontdoors and mixed VPI/VHPI HDL backdoors,
   standard register sequences, register callbacks and coverage, then integrate
   sequences/registers with debugger, activity, foreign state, artifacts,
   relocation, checkpoint/restart, replay, cache provenance, and isolation.
8. Changes 17-18 run representative unmodified UVM 1.2 and UVM 2020-3.1
   sequence and register environments through direct source, portable objects,
   O0/O2 interpreter/compiled cold/warm/debug paths, frontdoor/backdoor access,
   artifacts, relocation, replay, and retained resource caps. Change 19 freezes
   the complete positive/negative/race/cancellation matrices, docs, inventories,
   audits, evidence, and handoff.
9. Accumulate Changes 1-20 in one dirty Batch 161 worktree. Use at least eight
   workers for local builds, retain exact test and memory output, update this
   handoff after every completed change, and do not reset, commit, or push the
   implementation before Change 20.
10. Batch 161 is not a sanitizer or hosted-CI monitoring boundary. Do not run a
    sanitizer or inspect hosted CI for Batch 161 unless a new failure requires
    it. Change 20 instead runs fresh full non-sanitized exact-LLVM 22.1.8 Debug
    and Release eight-worker builds, all regressions, exact upstream sequence
    and register tests, memory/source audits, public/installed/portability
    contracts, and release gates before the single implementation commit/push.
11. Before Batch 162 implementation, repeat this flow: expand and save its exact
    restart plan, commit and push the documentation-only checkpoint, then clear
    context. Do not begin Batch 161 Change 1 until this checkpoint itself is
    committed, pushed, and followed by a context clear.
12. Next action after that clear: implement Batch 161 Change 1 only, beginning
    with simulation-owned, generation-checked sequence-item, sequence, and
    sequencer identities, exact ownership/lifecycle/registration, nominal
    request/response profiles, transactional construction, resource ceilings,
    cataloged invalid-handle/hierarchy/type/limit diagnostics, and focused
    evidence that does not execute sequence bodies early.
13. Change 1 is complete in the intentionally dirty Batch 161 worktree. Each
    `Simulation` directly owns a `SystemVerilogUvmSequenceService` with opaque
    generation-checked item, sequence, and sequencer handles; root-qualified
    identities; exact component, parent, child, item, and sequencer ownership;
    nominal request/response profiles; frozen lifecycle states; one source-order
    declaration stream; and dormant pre-start/pre-body/body/post-body/post-start
    callbacks. Registration and leaf-first release are transactional and
    resource bounded. Cataloged `FSIM-UVM-SEQ-001` through `006` failures cover
    stale backing state, foreign simulations, type/profile mismatches,
    hierarchy/root/name errors, lifecycle misuse, and every count, byte, depth,
    fanout, registration, and mutation ceiling without partial publication or
    skipped declaration order.
14. Focused runtime proof covers the positive ownership/lifecycle model, equal
    top-level names isolated across roots, rollback, stale/cross-owner handles,
    all diagnostics and limits, orderly invalidation, and zero callback calls
    before execution. Application proof covers direct public ownership through
    interpreter, compiled, and debug contexts, peer-simulation isolation, and
    clean fresh state. The eight-worker exact-LLVM Debug build completed 150
    steps warning-clean and rebuilt five affected steps after the final
    root-isolation audit. Final `fsim.runtime` and `fsim.application` pass 2/2
    in 30.60 seconds; `fsim.llvm`, `fsim.diagnostics-catalog`,
    `fsim.source-line-budget`, `fsim.uvm-source-harness`, and
    `fsim.installed-public-contract` pass 5/5 in 3.94 seconds. `git diff
    --check` is clean. Preserve all Change 1 files and proceed to Batch 161
    Change 2 without committing or pushing before Change 20.
15. Change 2 is complete in the intentionally dirty Batch 161 worktree. Sequence
    execution now advances pre-start, optional pre-body, body, optional
    post-body, post-start, ended, and finished states with one retained global
    event order. Nested children inherit their active parent phase and own real
    synchronous child identities in the task-phase process tree. Normal
    completion and recursive kill cancellation survive in the phase's final
    process snapshots before reclamation. Automatic objections bind each
    sequence object/root and balance raise/drop. Cooperative stop, recursive
    kill, cross-root/process misuse, callback exceptions, and cleanup failures
    are deterministic; callback failures are contained as cataloged
    `FSIM-UVM-SEQ-007` results. Typed response items route and pop FIFO from the
    owning sequence, and active execution, retained event/failure, response,
    plus mutation limits reject transactionally.
16. Runtime evidence covers exact nested callback and state order, reusable
    finished sequences, balanced objections, phase-process ancestry, completed
    and cancelled process states, stop/kill, response routing, callback failure
    containment, trace order, invalid lifecycle/cross-root cases, rollback, and
    every new ceiling. The application executes all five callbacks in its
    interpreter, compiled, and debug loop. The eight-worker exact-LLVM Debug
    build completed 81 affected steps warning-clean. `fsim.runtime`,
    `fsim.application`, `fsim.llvm`, `fsim.diagnostics-catalog`,
    `fsim.source-line-budget`, `fsim.uvm-source-harness`, and
    `fsim.installed-public-contract` pass 7/7 in 28.91 seconds. Preserve all
    Changes 1-2 files and proceed to Batch 161 Change 3 without committing or
    pushing before Change 20.
17. Change 3 is complete in the intentionally dirty Batch 161 worktree.
    Sequencers now own source-ordered, generation-checked request queues with
    positive bounded priorities, static/callback relevance, explicit wakeup,
    bounded wait-for-relevant state, and transactional selection/cancellation.
    FIFO, random, strict-FIFO, strict-random, weighted, and user arbitration are
    complete. Strict modes honor maximum priority and stable ties; random modes
    use independent per-sequencer SplitMix64 streams with unbiased bounded
    draws; weighted mode uses priority weights; user callbacks see only
    relevant candidates in stable order. Reseeding exactly replays a stream,
    and peer equal-seed streams do not consume each other's state. Invalid
    arbitration is cataloged as `FSIM-UVM-SEQ-008`; existing resource code
    `FSIM-UVM-SEQ-006` bounds wait starvation, selection/random work, pending
    queues, per-sequencer queues, identities, generations, and mutations.
18. Runtime evidence covers every mode, priorities and stable ties, relevance
    wakeup and callback failure, wait bounds, reseed replay, independent peer
    streams, weighted replay/distribution, user selection and failure, stale and
    foreign requests, rollback, and every new ceiling. The application selects
    its completed sequence in interpreter, compiled, and debug contexts. The
    eight-worker exact-LLVM Debug build completed 79 affected steps
    warning-clean. `fsim.runtime`, `fsim.application`, `fsim.llvm`,
    `fsim.diagnostics-catalog`, `fsim.source-line-budget`,
    `fsim.uvm-source-harness`, and `fsim.installed-public-contract` pass 7/7 in
    27.40 seconds. Preserve all Changes 1-3 files and proceed to Batch 161
    Change 4 without committing or pushing before Change 20.
19. Change 4 is complete in the intentionally dirty Batch 161 worktree.
    Sequencers now own generation-checked lock/grab identities, ordered pending
    queues, nested grant stacks, exact unlock/ungrab ownership, descendant
    nesting, grab precedence, bounded lock-starvation prevention, explicit
    cancellation, and leaf-first stop/kill cleanup. Response queues implement
    bounded error, drop-oldest, and drop-newest policy with exact dropped-item
    results and transactional reconfiguration. Cataloged `FSIM-UVM-SEQ-009`
    covers invalid response depth/policy/overflow action, while
    `FSIM-UVM-SEQ-010` covers invalid lock/grab ownership and operations.
    Item/sequence macro adapters factory-create and register objects, enqueue
    exact sequence/request-item identities, integrate completed transactional
    class randomization, and start randomized sequences. Provisional requests
    and their identity/order/mutation publication roll back on unsatisfiable,
    resource-exhausted, or throwing constraint configuration; equal object seed
    and call identity replay exactly, and failed random-start invokes no body.
20. Runtime evidence covers immediate and nested grants, grab precedence,
    starvation bounds, exact release kind/owner, pending/granted/stop
    cancellation, stale/foreign/unbound/duplicate negatives, access count/depth
    and policy ceilings, all response overflow/reconfiguration policies, macro
    create/send/random-send/random-start for items and sequences, inline/class
    constraints, replay, exceptions, and complete object/queue rollback. The
    application acquires/releases a lock and macro-sends its exact item in
    interpreter, compiled, and debug contexts. The eight-worker exact-LLVM
    Debug build completed 150 affected steps warning-clean. `fsim.runtime`,
    `fsim.application`, `fsim.llvm`, `fsim.diagnostics-catalog`,
    `fsim.source-line-budget`, `fsim.uvm-source-harness`, and
    `fsim.installed-public-contract` pass 7/7 in 36.91 seconds. Preserve all
    Changes 1-4 files and proceed to Batch 161 Change 5 without committing or
    pushing before Change 20.
21. Change 5 is complete in the intentionally dirty Batch 161 worktree. Each
    sequencer now owns bounded generation-checked driver-handshake transactions
    retaining the exact selected request snapshot, sequence/sequencer, optional
    request item, request object, optional response, pull/push kind, phase and
    process, deadline, state, completion order, and cancellation reason.
    Blocking get-next/get/peek/push distinguish waiting from nonblocking
    try-next empty; repeated peek returns one identity; get completes directly;
    get-next/try-next require item-done; and push FIFO, configurable pipelining,
    and backpressure preserve unconsumed arbitration order. Item-done and later
    put-response route exact owned responses. Timeout, process synchronization,
    the transaction-aware phase-jump wrapper, cooperative stop, and recursive
    kill retain distinct cancellations. Terminal snapshots persist until
    explicit stale-making release and block premature owned-object teardown.
    Cataloged `FSIM-UVM-SEQ-011` covers invalid handshakes, contexts, response
    associations, state transitions, cancellation reasons, and reconfiguration;
    `FSIM-UVM-SEQ-006` bounds every new count, depth, timeout, identity, order,
    and mutation resource.
22. Runtime evidence covers blocking/nonblocking waits, relevance, all pull
    forms, repeated peek/get identity, request-only sequence objects, three-deep
    pull pipelines, push FIFO and capacity backpressure, item-done and separate
    put-response, wrong-owner rollback, request/response retention, explicit,
    timeout, real process, actual phase-jump, stop, and kill cancellation,
    stale/foreign/cross-root misuse, live reconfiguration, lifecycle release,
    and every resource ceiling. The application completes its macro-sent exact
    item through get-next-item/item-done and inspects the retained transaction in
    interpreter, compiled, and debug contexts. The eight-worker exact-LLVM
    Debug build completed 150 affected steps warning-clean. `fsim.runtime`,
    `fsim.application`, `fsim.llvm`, `fsim.diagnostics-catalog`,
    `fsim.source-line-budget`, `fsim.uvm-source-harness`, and
    `fsim.installed-public-contract` pass 7/7 in 34.03 seconds. Preserve all
    Changes 1-5 files and proceed to Batch 161 Change 6 without committing or
    pushing before Change 20.
23. Change 6 is complete in the intentionally dirty Batch 161 worktree. The
    sequence service now owns generation-checked driver, monitor, agent,
    subscriber, and scoreboard role identities, snapshots, events, failures,
    source callback dispatch, and bounded lifecycle state. Root-qualified
    config-db lookup resolves each agent's exact active/passive mode during
    build. Drivers bind same-root sequencers, execute the completed handshake,
    and balance automatic run objections; passive agents reject drivers while
    retaining operational monitors. Monitors own typed TLM1 analysis ports and
    subscribers/scoreboards own typed implementations with deterministic
    same-root fanout. Checked endpoint release plus leaf-first role teardown
    removes connections and callbacks before component destruction, cancels
    root-local active work, makes role handles stale, and preserves peer roots.
    Cataloged `FSIM-UVM-SEQ-012` covers invalid role hierarchy, configuration,
    sequencer/analysis bindings, phase/process context, callbacks, objections,
    and operations; `FSIM-UVM-SEQ-006` bounds every new role, root, dispatch,
    event, failure, connection, publication, identity, order, and mutation
    resource.
24. Runtime evidence covers active/passive config overrides, exact
    build/connect/run lifecycle, driver get-next-item/item-done, subscriber and
    scoreboard fanout, passive monitoring, balanced objections, cross-root,
    duplicate, malformed, passive-driver, stale-handle, teardown-order, and
    count/dispatch/publication-limit cases with rollback and peer isolation.
    Application evidence dispatches active agent, driver, and passive agent
    roles beside actual SystemVerilog class build/connect/run callbacks through
    interpreter, compiled, and debug engines and proves fresh state. The final
    eight-worker exact-LLVM Debug build completed 174 affected steps
    warning-clean. `fsim.runtime`, `fsim.application`, `fsim.llvm`,
    `fsim.diagnostics-catalog`, `fsim.source-line-budget`,
    `fsim.uvm-source-harness`, and `fsim.installed-public-contract` pass 7/7 in
    31.61 seconds. `git diff --check` is clean. Preserve all Changes 1-6 files
    and proceed to Batch 161 Change 7 without committing or pushing before
    Change 20.
25. Change 7 is complete in the intentionally dirty Batch 161 worktree.
    Existing generation-checked sequencer identities now retain explicit
    virtual/domain metadata, a single virtual parent, unique names, exact child
    handles and request/response profiles, and common-root ownership. Virtual
    sequences reuse the completed lifecycle and coordinate only from an active
    body with a live task-phase process. Each validated child step applies its
    priority through the request queue, optional lock/grab through the access
    service, automatic objection through the objection service, and start
    through a real child of the virtual phase process. The resulting A/B child
    processes are exact siblings even though their typed sequencers are
    separate components.
26. Coordinated reset cancels only the active child transaction/process with
    `VirtualReset`, restores the epoch's response baselines, clears unconsumed
    requests/access, and replays the plan within a bounded restart count.
    Cooperative stop, recursive parent kill, child failure, and lost process
    state stop the remaining plan without leaked request, response, or access
    state; parent kill follows ephemeral cross-domain children as well as
    ordinary sequence children. `FSIM-UVM-SEQ-013` catalogs invalid virtual
    bindings/operations and `FSIM-UVM-SEQ-006` bounds domains, steps, restarts,
    and events. Runtime proof covers distinct A/B profiles, 700/900 priorities,
    lock/grab, balanced objections, reset/restart, exact sibling ancestry,
    response de-duplication, parent kill, cross-root/lifecycle/release
    negatives, rollback, and every new limit. Application proof executes a
    priority-161 locked virtual child from the real Run-phase driver process in
    interpreter, compiled, and debug engines; its helper was split into a
    dedicated source so `fsim.source-line-budget` remains green. The
    eight-worker exact-LLVM Debug build completed 166 affected steps
    warning-clean. Before the helper split, `fsim.runtime` and
    `fsim.application` passed 2/2 in 30.04 seconds; after the split,
    `fsim.application` and `fsim.source-line-budget` passed 2/2 in 29.16
    seconds. Final `fsim.runtime`, `fsim.application`, `fsim.llvm`,
    `fsim.diagnostics-catalog`, `fsim.source-line-budget`,
    `fsim.uvm-source-harness`, and `fsim.installed-public-contract` pass 7/7 in
    32.96 seconds. Preserve all Changes 1-7 files and proceed to Batch 161
    Change 8 without committing or pushing before Change 20.
27. Change 8 is complete in the intentionally dirty Batch 161 worktree. A
    simulation-owned callback service now provides generation-checked type-wide
    and exact-instance registrations, global append/prepend order, masks,
    frozen iterator-safe mutation, immutable routing, exception containment,
    attribute rollback, bounded recursive re-entry, and retained failures.
    `FSIM-UVM-CALLBACK-001` catalogs invalid callback handles, ownership,
    scopes, types, masks, and operations; `FSIM-UVM-CALLBACK-002` bounds
    callbacks, fanout, failures, text, attributes, mutations, and re-entry.
    Runtime proof covers exact matching and order, delete-before-turn, deferred
    addition, disabling, exception continuation, stale/cross-service handles,
    all zero limits, and live count/fanout/failure/attribute ceilings.
28. The paired transaction recorder retains stable pointer-free identities,
    same-root parents and links, typed attributes, scheduler time/delta,
    begin/attribute/link/end trace records, completed/cancelled/failed state,
    callback masks, activity/VCD events, and safe terminal release. Activity
    rejection rolls snapshot, trace, and lifecycle state back atomically.
    `FSIM-UVM-TR-001` catalogs invalid handles, roots, parents, links, states,
    attributes, and values; `FSIM-UVM-TR-002` bounds recorder resources.
    Runtime proof covers parent/child identity, exact callback order, contiguous
    traces, timing, typed and callback-added attributes, lifecycle negatives,
    aggregate bytes, capacity rollback, and every zero/live limit. Application
    proof records a source-backed Run-phase transaction and proves callback
    order, completed state, debugger output, and identical stable trace
    signatures in interpreter, compiled, and debug engines. The eight-worker
    exact-LLVM Debug build completed 79 affected steps warning-clean;
    `fsim.runtime`, `fsim.application`, `fsim.application.classes`, `fsim.llvm`,
    `fsim.diagnostics-catalog`, `fsim.source-line-budget`,
    `fsim.uvm-source-harness`, and `fsim.installed-public-contract` pass 8/8 in
    39.36 seconds. `git diff --check` is clean. Preserve all Changes 1-8 files
    and proceed to Batch 161 Change 9 without committing or pushing before
    Change 20.
29. Change 9 is complete in the intentionally dirty Batch 161 worktree. A
    simulation-owned register-model service now provides strongly typed block,
    map, register, field, and memory handles with stable numeric identities,
    exact root/parent/depth ownership, deterministic root-qualified full names,
    and one globally contiguous mixed-kind declaration stream. Blocks reject
    duplicate names across child kinds; registers reject duplicate,
    overlapping, and out-of-range fields. Registers, fields, maps, and
    multidimensional memories retain checked widths, offsets, dimensions, and
    word counts, including exact 64-bit span-overflow rejection. Locking through
    a top block revalidates the live root and recursively freezes its complete
    child-block hierarchy.
30. `FSIM-UVM-REG-001` catalogs invalid handles, roots, hierarchy, ownership,
    names, shapes, fields, address spans, build state, and lock operations;
    `FSIM-UVM-REG-002` bounds every node count, global/per-owner declarations,
    depth, text, width, dimension, extent, word count, identity, order, and
    mutation resource. Runtime proof covers all typed metadata, exact source
    order, recursive freeze, stale/cross-service/cross-root handles, stale root,
    duplicate names, field fit/overlap, memory shape, overflow, every zero limit,
    and live saturation of every resource category. The application locks a
    source-backed model and hashes every stable declaration field into the
    existing interpreter/compiled/debug signature, proving engine equality and
    fresh-simulation isolation. Graph audit reports maximum cognitive
    complexity 16, loop depth 1, and no scan-in-loop hotspot. The final
    eight-worker exact-LLVM Debug build completed 97 affected steps
    warning-clean; `fsim.runtime`, `fsim.application`,
    `fsim.application.classes`, `fsim.llvm`, `fsim.diagnostics-catalog`,
    `fsim.source-line-budget`, `fsim.uvm-source-harness`, and
    `fsim.installed-public-contract` pass 8/8 in 41.44 seconds. Preserve all
    Changes 1-9 files and proceed to Batch 161 Change 10 without committing or
    pushing before Change 20.
31. Change 10 is complete in the intentionally dirty Batch 161 worktree. The
    register model implements all twenty-six standard field access policies,
    access-aware desired values, independent mirrors, volatile update
    detection, compare masks, direct/read/write prediction, pre-side-effect
    read results, read-clear/read-set behavior, W1/WO1 physical write-once
    state, and named exact-width resets. Only HARD reset rearms W1/WO1 fields.
    Register operations preserve non-writable modeled slices. Multidimensional
    memories use checked row-major indices and sparse desired/mirrored words.
    Invalid widths, X/Z values, rights, prediction kinds, reset kinds, indices,
    and all operation/storage ceilings reject atomically through cataloged
    `FSIM-UVM-REG-003` and `FSIM-UVM-REG-004` results.
32. Runtime proof enumerates every policy across set, first/second writes,
    SOFT/HARD reset, read result, and read side effects, plus mixed registers,
    volatile/compare behavior, prediction, multidimensional memory, rights,
    rollback, and every new zero/live/aggregate ceiling. Application proof
    resets, writes, reads, and hashes stable value state identically through
    interpreter, compiled, and debug engines. The final eight-worker exact-LLVM
    Debug build completed 151 steps warning-clean. `fsim.runtime`,
    `fsim.application`, `fsim.application.classes`, `fsim.llvm`,
    `fsim.diagnostics-catalog`, `fsim.source-line-budget`,
    `fsim.uvm-source-harness`, and `fsim.installed-public-contract` pass 8/8
    in 39.94 seconds. A direct runtime test passes in 0.02 seconds with maximum
    RSS 14,092 KiB; `git diff --check` is clean. Preserve all Changes 1-10 and
    proceed to Batch 161 Change 11 without committing or pushing before Change
    20.
33. Change 11 is complete in the intentionally dirty Batch 161 worktree. The
    register model now retains byte- or word-addressed maps, exact bus widths,
    all four little/big/FIFO endian modes, hierarchical submaps using the
    narrowest bus, multiple-map membership, independent per-map rights,
    explicitly unmapped entries, stable direct/hierarchical inventories, exact
    register-beat and multidimensional-memory-word lookup, and dynamic physical
    beat byte enables. Locking transactionally validates address-unit and bus
    alignment, checked 64-bit spans, and register, memory, and submap overlap.
    Policy-aware burst writes preserve disabled bytes, and sparse touched-word
    backup restores desired, mirrored, written, operation, mutation, and
    materialized-bit state after injected mid-burst failure. Cataloged
    `FSIM-UVM-REG-005` covers invalid map construction/access and
    `FSIM-UVM-REG-006` bounds memberships, hierarchy, bus/beat/byte-enable,
    burst/value-bit, lookup/work, identity, order, and mutation resources.
34. Runtime evidence covers both address-unit modes, every endian profile,
    hierarchical addresses, partial beats, inventories, lookup, RO/WO and
    unmapped behavior, policy-aware byte writes, bursts, transactional
    alignment/overlap/overflow rejection, and every new zero/live ceiling. The
    application maps a source-backed status register and memory, performs burst
    traffic and lookup, and hashes stable map configuration/inventory equally
    through interpreter, compiled, and debug engines. The final eight-worker
    exact-LLVM Debug build completed 24 affected steps warning-clean.
    `fsim.runtime`, `fsim.application`, `fsim.application.classes`, `fsim.llvm`,
    `fsim.diagnostics-catalog`, `fsim.source-line-budget`,
    `fsim.uvm-source-harness`, and `fsim.installed-public-contract` pass 8/8 in
    41.78 seconds. A direct runtime test passes in 0.02 seconds with maximum RSS
    14,260 KiB. Graph audit reports maximum cognitive complexity 19, loop depth
    2, and no recursion; heuristic scan flags are bounded dimension walks or
    logarithmic ordered/sparse lookups, and overlap detection is sort plus
    adjacent comparison. `git diff --check` is clean. Preserve Changes 1-11 and
    proceed to Batch 161 Change 12 without committing or pushing before Change
    20.
35. Change 12 is complete in the intentionally dirty Batch 161 worktree.
    Register adapters own exact root, sequencer, nonvirtual sequence, priority,
    auto-predict policy, and conversion/drive callbacks. Generic bus items
    retain selected map/target, exact address/data/byte enables, logical byte
    offset, beat index/count, and operation order. Register and memory
    read/write, register update/mirror, clean no-op update, multi-beat response
    assembly, mismatch checking, auto-prediction, explicit register/memory
    prediction, and TLM analysis predictors execute through real sequence
    items and retained handshake transactions. Explicit completion, timeout,
    phase-owned and explicit cancellation retain deterministic terminal state.
    Callback, response, ownership, rights, and prediction failures are
    contained without partial mirror publication. Cataloged
    `FSIM-UVM-REG-007` covers invalid frontdoor state and
    `FSIM-UVM-REG-008` bounds every adapter, predictor, operation, pending,
    bus-item, value-bit, byte-enable, observation, identity, order, and mutation
    resource.
36. Runtime evidence covers exact multi-beat register/memory traffic,
    write/read/update/no-op/mirror, auto/explicit/TLM prediction,
    timeout/phase/explicit cancellation, cross-root and rights rejection,
    failed status, callback exceptions, invalid response ownership, unmapped
    prediction, rollback, and every new zero/live ceiling. Application proof
    executes real frontdoor write, mirror, update, and memory read with exact
    sequence items and transactions in interpreter, compiled, and debug
    engines, including stable trace hashing. The final eight-worker exact-LLVM
    Debug build completed 77 affected steps warning-clean. `fsim.runtime`,
    `fsim.application`, `fsim.application.classes`, `fsim.llvm`,
    `fsim.diagnostics-catalog`, `fsim.source-line-budget`,
    `fsim.uvm-source-harness`, and `fsim.installed-public-contract` pass 8/8 in
    45.41 seconds. Direct runtime passes in 0.02 seconds with maximum RSS 14,324
    KiB. The graph reports maximum cognitive complexity 19, loop depth 1, no
    recursion, and only bounded physical-beat rights-check scan flags. `git
    diff --check` is clean. Preserve Changes 1-12 and proceed to Batch 161
    Change 13 without committing or pushing before Change 20.
37. Change 13 is complete in the intentionally dirty Batch 161 worktree. User
    frontdoors retain exact roots, register/field/memory-word targets, and
    contained read/write callbacks. HDL paths retain ordered VPI/VHPI slices,
    canonical names, exact physical/logical placement, stable inventories, and
    complete nonoverlapping logical coverage. Registration and every access
    re-resolve canonical paths, preventing stale engine identities after
    relocation. Reads assemble mixed-language concatenations and predict the
    selected target. Deposit and force preserve outside-slice bits; release
    re-reads and predicts the deposited value; preflight and rollback prevent
    partial concatenated writes. Cataloged `FSIM-UVM-REG-009` covers invalid
    frontdoor, target, callback, HDL path/slice, resolution, access, relocation,
    and rollback state. `FSIM-UVM-REG-010` bounds frontdoor, path, slice, text,
    operation, value-bit, identity, order, and mutation resources.
38. Runtime evidence covers user callbacks and containment, mixed VPI/VHPI
    concatenations, register/field/sparse-memory paths, read/deposit/force/
    release, physical-bit preservation, rollback, overlap rejection,
    canonical-path removal/recreation, and every zero/live resource ceiling.
    Application proof executes a real user frontdoor and canonical VPI-backed
    design-signal read with stable state across interpreter, compiled, and
    debug engines. The final eight-worker exact-LLVM Debug build completed 77
    affected steps warning-clean. `fsim.runtime`, `fsim.application`,
    `fsim.application.classes`, `fsim.llvm`, `fsim.diagnostics-catalog`,
    `fsim.source-line-budget`, `fsim.uvm-source-harness`, and
    `fsim.installed-public-contract` pass 8/8 in 41.02 seconds. Direct runtime
    passes in 0.02 seconds with maximum RSS 14,196 KiB. Graph audit reports
    maximum cognitive complexity 18, loop depth 2, no recursion, and no
    backdoor scan-in-loop flags. `git diff --check` is clean. Preserve Changes
    1-13 and proceed to Batch 161 Change 14 without committing or pushing
    before Change 20.
39. Change 14 is complete in the intentionally dirty Batch 161 worktree. The
    standard register-sequence service runs reset, hardware-reset, bit-bash,
    register access, shared access, memory access, memory walk, and model
    traversal against a locked root and optional selected hierarchical map.
    Exact full-name/subtree exclusions carry per-kind masks. Map rights,
    unmapped entries, field access/volatility/compare policies, and memory
    access policies suppress illegal operations. Hardware reset contains its
    DUT callback before reset/mirror checking. Bit bash restores every eligible
    field; access/shared-access use deterministic SplitMix64 streams and restore
    registers; memory sequences cover every selected multidimensional word.
    Callback or direct-model reads/writes validate status and exact width while
    retaining bounded failures. `FSIM-UVM-REG-011` catalogs invalid sequence
    ownership/configuration/access state and `FSIM-UVM-REG-012` bounds retained
    sequences, operations, failures/text, exclusions/text, identities, order,
    and mutation.
40. Runtime evidence covers all eight kinds, exact resets, bit and word
    traversal, multiply mapped registers, RO suppression, deterministic replay,
    exclusions, callback containment, invalid selections, and every new
    zero/live/aggregate ceiling. Application proof executes and hashes one
    seeded access sequence equally across interpreter, compiled, and debug
    engines. The final eight-worker exact-LLVM Debug build completed 24 affected
    steps warning-clean. `fsim.runtime`, `fsim.application`,
    `fsim.application.classes`, `fsim.llvm`, `fsim.diagnostics-catalog`,
    `fsim.source-line-budget`, `fsim.uvm-source-harness`, and
    `fsim.installed-public-contract` pass 8/8 in 40.32 seconds. Direct runtime
    passes in 0.02 seconds with maximum RSS 14,468 KiB. Graph audit reports
    maximum cognitive complexity 17, loop depth 2, no recursion, and no
    scan-in-loop flags. `git diff --check` is clean. Preserve Changes 1-14 and
    proceed to Batch 161 Change 15 without committing or pushing before Change
    20.
41. Change 15 is complete in the intentionally dirty Batch 161 worktree.
    Callback-aware register, field, and multidimensional memory-word accesses
    own block, map, register, memory, and field callbacks with exact phase masks,
    signed priority, identity, and order. Pre dispatch runs outer-to-inner and
    post dispatch reverses it. Mutable value/status contexts reach physical
    writes and final read prediction, while target/map/phase/width mutation,
    explicit stop, exceptions, ownership, and selected-map-right failures are
    contained and counted. Standard register sequences reuse this path.
    Coverage models select locked block subtrees and retain per-map read/write,
    per-field mirrored-value, and named-reset/desired/mirror cross bins.
    `FSIM-UVM-REG-013` catalogs invalid callback/coverage/access state and
    `FSIM-UVM-REG-014` bounds callbacks, per-access dispatch, invocations,
    failures, coverage models/samples/bins/text, identities, order, and mutation.
42. Runtime proof covers all five scopes/four phases, stable order/priority,
    valid and invalid mutation, memory words, exception containment, RO
    rejection before dispatch, exact coverage bins/crosses, inventories,
    sequence coexistence, and every new zero/live/aggregate ceiling. Application
    proof runs four scoped callbacks and one coverage model through a seeded
    standard access sequence and hashes exact invocation/sample/bin state across
    interpreter, compiled, and debug engines. The final eight-worker exact-LLVM
    Debug build completed 77 affected steps warning-clean. `fsim.runtime`,
    `fsim.application`, `fsim.application.classes`, `fsim.llvm`,
    `fsim.diagnostics-catalog`, `fsim.source-line-budget`,
    `fsim.uvm-source-harness`, and `fsim.installed-public-contract` pass 8/8 in
    40.03 seconds. Direct runtime passes in 0.02 seconds with maximum RSS 14,520
    KiB. Graph audit reports maximum cognitive complexity 18, loop depth 1, no
    recursion, and no scan-in-loop flags. `git diff --check` is clean. Preserve
    Changes 1-15 and proceed to Batch 161 Change 16 without committing or
    pushing before Change 20.
43. Change 16 is complete in the intentionally dirty Batch 161 worktree.
    `uvm sequences`, `uvm registers`, and `break uvm IDENTITY|*` expose the new
    services through the existing bounded debugger and activity surfaces.
    Sequence registration/execution plus register callback, coverage, and
    standard-sequence operations publish contained activity that automatically
    reaches VCD and DPI/VPI/VHPI observers. The append-only foreign ABI retains
    version 1 and its exact C layouts while adding thirteen sequence/callback/
    transaction/register record kinds. Application-owned foreign hosts encode
    canonical identities, ordering, RNG replay state, desired/mirrored/reset
    values, transaction state, callbacks, and coverage without host pointers.
    Checkpoint validation and the `FSIMUVM1` portable codec retain exact content,
    cache, artifact, and root provenance. Integrated record/text ceilings are
    enforced before each intermediate record is retained, preventing oversized
    models from ballooning memory before a resource rejection.
44. Application proof formats both debugger sections, observes both activity
    kinds, round-trips the completed virtual sequence/register checkpoint,
    rejects a one-record-short ceiling, and finds sequence, mirrored-field, and
    seeded standard-sequence foreign records. Existing relocated-artifact,
    wrong-simulation handle, fresh-context, engine-signature, and cold/warm-
    cache proofs retain deterministic replay and isolation. The final
    eight-worker exact-LLVM Debug build completed 81 affected steps warning-
    clean. `fsim.runtime`, `fsim.application`, `fsim.application.classes`,
    `fsim.llvm`, `fsim.diagnostics-catalog`, `fsim.source-line-budget`,
    `fsim.uvm-source-harness`, and `fsim.installed-public-contract` pass 8/8 in
    38.80 seconds. Direct runtime passes in 0.02 seconds with maximum RSS 14,320
    KiB. Graph audit of the new foreign collector reports cognitive complexity
    31, loop depth 2, no scan-in-loop flag, and no recursion. `git diff --check`
    is clean. Preserve Changes 1-16 and proceed to Batch 161 Change 17 without
    committing or pushing before Change 20.
45. Change 17 is complete in the intentionally dirty Batch 161 worktree. One
    exact source environment imports the unmodified UVM 1.2 and UVM 2020-3.1
    sequence item, sequence, sequencer, driver, monitor, agent, scoreboard,
    environment, and callback surfaces. It proves strict-FIFO arbitration,
    lock plus pending grab ownership, request/response completion, monitor-to-
    scoreboard analysis, a real phase-process virtual sequence and child,
    type-wide plus instance callback order, and transaction recording. Exact
    parameterized upstream classes whose specialization ancestry ends early are
    recognized through inherited property and canonical method ownership.
    Explicit object/component allocation registers the requested specialization
    and transactionally enforces initialized component ownership.
46. The exact release matrices pass direct source, portable `.fsimobj`, O0/O2
    `.fsimdesign`, interpreter, compiled cold/warm native cache, and debug
    execution with the stable suffix `sequence=arb/lock/response/virtual
    roles=agent/driver/monitor/scoreboard callback=6 transaction=5 cap=records`.
    UVM 1.2 passes in 398.26 seconds and UVM 2020-3.1 passes in 497.39 seconds.
    The final eight-worker exact-LLVM Debug affected build completes 68 steps
    warning-clean. `fsim.runtime`, `fsim.application`,
    `fsim.application.classes`, `fsim.llvm`, `fsim.diagnostics-catalog`,
    `fsim.source-line-budget`, `fsim.uvm-source-harness`, and
    `fsim.installed-public-contract` pass 8/8 in 29.88 seconds. Direct runtime
    passes in 0.02 seconds with maximum RSS 14,400 KiB. Graph audit reports
    maximum cognitive complexity 17, loop depth 2, one bounded inventory
    scan-in-loop flag, and no recursion. `git diff --check` is clean. Preserve
    Changes 1-17 and proceed to Batch 161 Change 18 without committing or
    pushing before Change 20.
47. Change 18 is complete in the intentionally dirty Batch 161 worktree. The
    exact fixture derives register, register-block, adapter, predictor,
    register-sequence, and register-callback classes from both unmodified
    upstream releases and allocates them in a dedicated exact register
    environment. Its simulation-owned model drives a 32-bit register and
    memory through little- and big-endian 16-bit maps, exact two-beat
    frontdoors, `1010` byte enables, reset/desired/mirror state, TLM analysis
    prediction, combined VPI/VHPI backdoor deposit/read/force/release, a scoped
    callback, coverage, and the seeded access sequence. The integrated
    checkpoint round-trips from a relocated origin and rejects a one-record-
    short cap without partial capture.
48. The stable suffix is `register=frontdoor/backdoor/predictor
    maps=little/big byte_enable=1010 callback_coverage=1 sequence=access
    replay=relocated cap=records`. Direct source, portable `.fsimobj`, O0/O2
    `.fsimdesign`, interpreter, compiled cold/warm native cache, and debug pass
    for exact UVM 1.2 in 432.29 seconds and UVM 2020-3.1 in 541.39 seconds, 2/2
    in 973.69 seconds overall. The final eight-worker exact-LLVM Debug affected
    build completes three steps warning-clean. `fsim.runtime`,
    `fsim.application`, `fsim.application.classes`, `fsim.llvm`,
    `fsim.diagnostics-catalog`, `fsim.source-line-budget`,
    `fsim.uvm-source-harness`, and `fsim.installed-public-contract` pass 8/8 in
    27.53 seconds. Direct runtime passes in 0.02 seconds with maximum RSS 14,364
    KiB. Graph audit reports cognitive complexity 11, loop depth 2, no
    scan-in-loop flag, and no recursion. `git diff --check` is clean. Preserve
    Changes 1-18 and proceed to Batch 161 Change 19 without committing or
    pushing before Change 20.
49. Change 19 is complete in the intentionally dirty Batch 161 worktree. The
    aggregate UVM matrix scans the full phase/TLM, sequence, role, callback/
    transaction, and register evidence set plus both exact-source probes,
    freezes all 64 cataloged UVM diagnostic families, and requires
    representative arbitration, access, handshake, role, virtual-sequence,
    callback, transaction, endian-map, predictor, mixed-VPI/VHPI, standard-
    sequence, coverage, exact-sequence, and exact-register tokens. README, the
    public UVM guide/transcript, architecture, language support, diagnostics,
    provenance/resource baselines, feature matrix, release audits, inventory,
    and release-candidate corpus now describe one Batch 161 boundary.
50. The reviewed inventory is 2,054 diagnostics, 809 bounded C/C++ sources,
    910 SPDX-owned authored files, and 305 authored test/control files. The
    release matrix is 1,262 executable rows with 5,048 evidence cells across
    579 exact paths (254 test, 303 production, and 22 release) and 138 runtime
    owners. Matrix/evidence SHA-256 values are
    `3f6bc26424dd6dabe984f1efbef858808c2370b50c0969c6d82340499410e506`
    and `ca0f3e8d6e9eff61b4949b948e7c539ad61d6ea25031e1b547ec5b79240dafc2`.
    The complete documentation, conformance, inventory, installed-public, and
    portability tranche passes 28/28 in 18.16 seconds with maximum RSS 57,108
    KiB. `git diff --check` is clean. Preserve Changes 1-19 and proceed to
    Change 20 without committing or pushing.
51. Change 20 and Batch 161 are complete in the accumulated implementation
    worktree. Clean-first exact-LLVM 22.1.8 Debug and Release eight-worker builds
    each complete 721 steps warning-free in 9:05.64 and 7:52.60 with peak RSS
    4,035,212 and 2,485,712 KiB. Their complete regressions pass 118/118 in
    384.37 and 323.00 seconds with peak RSS 3,779,552 and 3,787,920 KiB. The
    dedicated governed-UVM tree refreshes 201 steps warning-free in 2:59.64
    with peak RSS 3,982,708 KiB.
52. Exact UVM 1.2 passes in 467.47 seconds and exact UVM 2020-3.1 passes in
    566.45 seconds, 2/2 in 1,033.92 seconds with peak RSS 4,775,752 KiB. Every
    direct, portable-object, O0/O2 design, compiled cold/warm, and debug stage
    emits the full sequence/register transcript. All fourteen traces are 17,833
    bytes with SHA-256
    `95caf4dbb04fa7f1b1397df9b40e03a1fdbc19b90c6387221a8b59635ebd5eff`.
    Final audits retain 2,054 diagnostics, 809 bounded sources, 910 SPDX-owned
    files, 305 test/control owners, 1,262 executable rows, 5,048 evidence cells,
    579 evidence paths, and 138 runtime owners. No sanitizer or hosted-CI
    inspection ran at this non-monitoring boundary. Run the post-documentation
    contracts, commit and push the single Changes 1-20 implementation, then
    save/push the Batch 162 restart plan and clear context before implementing
    Batch 162.

## Batch 160 planned restart checkpoint - 2026-08-08

1. Start in `/home/colin/projects/fsim`, read this file and the authoritative
   expanded Batch 160 allocation in `implementation_plan_v2.md`, and verify
   branch `codex/v2` is clean and synchronized at pushed Batch 159 closeout
   `a7b2ab310e1b3e21e31e98f7dd387304f33cf749` plus this documentation-only
   Batch 160 planning checkpoint.
2. This checkpoint implements the required pre-batch flow. Batch 160 is
   expanded into twenty exact changes without broadening the locked UVM phase,
   objection, and TLM scope. No Batch 160 implementation file has changed;
   clear context after pushing this plan and resume only from this section and
   the authoritative allocation.
3. Preserve the complete Batch 159 UVM object/factory/configuration/reporting
   implementation and its bounded hydration repair. The final exact LLVM
   22.1.8 Debug and Release trees passed 115/115 in 414.95 and 336.28 seconds,
   and the accumulated implementation is pushed as `a7b2ab3`.
4. Change 1 begins with a simulation-owned phase model, not scheduler shortcuts:
   generation-checked phase/domain identities, common and runtime kinds/states,
   explicit graph ownership and relationships, per-root participation, custom
   registration order, transactional acyclic construction, resource ceilings,
   focused service evidence, and cataloged invalid-graph diagnostics.
5. Changes 2-4 construct the standard/custom graphs and then execute function
   and task phases with exact hierarchy ordering, domains, synchronization,
   jumps, hooks, process ownership, and bounded unmodified-UVM memory evidence.
   Do not implement objections or TLM early to disguise incomplete phase
   scheduling.
6. Changes 5-8 own objections, propagation, drains, all-dropped and ready/end
   hooks, phase-created process cancellation, multi-root/domain quiescence,
   deadlock/livelock bounds, teardown, and restart isolation. Changes 9-12 then
   own TLM1 ports/exports/imps and transport/FIFOs/analysis plus TLM2 sockets,
   payloads, phases, extensions, timing, and connection validation.
7. Changes 13-16 integrate the completed phase/TLM model with debugger,
   callbacks, VCD/FST, DPI/VPI, portable artifacts, relocation, deterministic
   replay, multiple simulations, and cache provenance. Changes 17-19 own exact
   unmodified UVM 1.2/2020 examples, capped resource measurements, aggregate
   positive/negative/race/deadlock matrices, docs, inventories, audits, and the
   handoff.
8. Accumulate Changes 1-20 in one dirty Batch 160 worktree. Use at least eight
   workers for local builds, retain exact test and memory output, update this
   handoff after every completed change, and do not reset, commit, or push the
   implementation before Change 20.
9. Batch 160 is the scheduled sanitizer and hosted-CI monitoring boundary.
   Only Change 20 runs the LLVM-disabled ASan/UBSan regression and full fresh
   exact-LLVM Debug/Release qualification, commits and pushes once, then
   inspects and repairs every non-documentation Linux, Windows, sanitizer, and
   fuzz job until green.
10. Before Batch 161 implementation, repeat this flow: expand and save its
    exact restart plan, commit and push the documentation-only checkpoint, then
    clear context. Do not begin Batch 160 Change 1 until this checkpoint itself
    is committed, pushed, and followed by a context clear.
11. Change 1 is complete in the intentionally dirty Batch 160 worktree. The
    simulation-owned phase service now provides opaque generation-checked
    domain/phase handles, exact standard kinds/states/identities, parent and
    predecessor/successor relationships, per-root participation, source-order
    custom registration, bounded topological traversal, transactional DAG
    mutation, and cataloged `FSIM-UVM-PHASE-001` through `005` failures.
12. Runtime coverage proves standard/custom graph construction, snapshots,
    source order, traversal, disconnect/reconnect, stale/cross-owner handles,
    duplicates, cycles, missing roots, rollback, and every identity, depth,
    node, edge, root, traversal, and mutation ceiling. Application coverage
    proves the public service belongs to its `Simulation`, peer simulations
    reject each other's handles, and a fresh simulation has no phase domains.
    The eight-worker exact-LLVM Debug full build passed all 147 steps. The
    focused runtime, application, LLVM, diagnostic-catalog, source-line,
    and UVM-source-harness gates passed 6/6; the final ownership-specific
    `fsim.application` rerun passed in 28.43 seconds. The final catalog and
    source-line gates pass 2/2, and `git diff --check` is clean after this
    handoff update.
13. Change 2 is complete in the intentionally dirty Batch 160 worktree. The
    service constructs the exact nine-node common and twelve-node runtime
    chains, places runtime with common `run`, and enrolls API-created and
    automatic component roots in both domains. Source-ordered custom tail,
    before, after, between, and parallel-with insertion rewires adjacency
    transactionally. Independent/shared domain placement, matching-identity and
    explicit phase synchronization, two-way removal, and deterministic
    topological order are frozen by focused positive evidence.
14. Invalid mixed/cross-domain/reverse anchors, duplicate standard schedules,
    self and cyclic domain placement, malformed or duplicate synchronization,
    and node, edge, root, and mutation exhaustion all reject without partial
    state. Standard-root participation reserves its complete two-domain cleanup
    budget, and failed automatic component construction cannot skip component-
    root destruction if phase cleanup throws. The eight-worker exact-LLVM Debug
    build completed 75 affected steps warning-clean. `fsim.runtime`,
    `fsim.application`, `fsim.llvm`, `fsim.diagnostics-catalog`,
    `fsim.source-line-budget`, and `fsim.uvm-source-harness` passed 6/6 in
    29.56 seconds; the application gate itself passed in 26.29 seconds.
15. Change 3 is complete in the intentionally dirty Batch 160 worktree. The
    function-phase executor owns exact scheduled-through-done transitions,
    top-down build and bottom-up later traversal, deterministic multiple roots,
    per-component started/execute/ready-to-end/ended hooks, and contained
    `FSIM-UVM-PHASE-006` failures. Build callbacks may add only direct children
    of the active component; other growth, root creation, teardown, task-phase
    use, and re-execution reject without corrupting hierarchy or phase state.
16. The application bridge resolves virtual source callbacks by dynamic
    specialization and proves build, connect, end-of-elaboration,
    start-of-simulation, extract, check, report, and final callbacks and exact
    ordering through interpreter, compiled, and debug engines. Runtime evidence
    additionally proves live build growth, all hooks/states, exception
    containment, mutation rules, and deterministic two-root traversal. The
    source-method implementation is split into a 592-line fragment and
    `application_simulation.cpp` is 1,934 lines. The eight-worker exact-LLVM
    Debug tree rebuilt 77 affected steps, followed by a warning-clean 15-step
    extraction rebuild. `fsim.runtime`, `fsim.application`, `fsim.llvm`,
    `fsim.diagnostics-catalog`, `fsim.source-line-budget`, and
    `fsim.uvm-source-harness` pass 6/6 in 32.33 seconds; `git diff --check` is
    clean.
17. Change 4 is complete in the intentionally dirty Batch 160 worktree. Every
    standard/custom task phase launches deterministic top-down, generation-
    checked process ownership by phase, root, and component. Immediate tasks
    complete synchronously; suspended tasks keep their phase executing until
    all owned processes complete or cancel, then ready/end hooks advance the
    phase to done. Callback failures are contained as `FSIM-UVM-PHASE-006`;
    invalid process, completion, synchronized-group, and jump state rejects as
    `FSIM-UVM-PHASE-007`, and process exhaustion rejects before publication.
18. Explicitly synchronized custom phases in sibling domains execute
    concurrently with independent roots/processes. Forward jumps cancel source
    work, skip the intervening interval, and leave the target executable;
    backward jumps cancel active work and reset the exact target-through-source
    interval. Runtime evidence covers run, all twelve runtime families, custom
    phases, synchronization, completion, cancellation, ownership, mutation
    guards, limits, and both jump directions. Source dispatch accepts zero- or
    one-phase-argument tasks and proves every standard task callback through
    interpreter, compiled, and debug engines.
19. The eight-worker exact-LLVM Debug tree rebuilt 75 affected steps warning-
    clean; `fsim.runtime`, `fsim.application`, `fsim.llvm`,
    `fsim.diagnostics-catalog`, `fsim.source-line-budget`, and
    `fsim.uvm-source-harness` pass 6/6 in 33.62 seconds. The main simulation
    source is 1,900 lines after extracting its 111-line UVM phase fragment.
    Final unmodified UVM 1.2 and UVM 2020-3.1 analysis under 3 GiB exits zero at
    1,856,684 KiB and 2,126,780 KiB peak RSS. Retained two-root compiled designs
    produce identical PASS transcripts under 4/5-GiB caps at 859,788 KiB and
    956,912 KiB peak RSS. Keep `/tmp/fsim-uvm12-b160-change4-final.log`,
    `/tmp/fsim-uvm12-b160-change4-final-time.txt`,
    `/tmp/fsim-uvm2020-b160-change4-final.log`,
    `/tmp/fsim-uvm2020-b160-change4-final-time.txt`, and the corresponding
    `*-exec*` logs as exact local evidence. `git diff --check` is clean.
20. Change 5 is complete in the intentionally dirty Batch 160 worktree. A
    dedicated simulation-owned objection service binds live UVM objects into
    owner-qualified source handles and owns exact phase/root/source/description
    entries plus source-local, propagated component, and root counts. Component
    raises, drops, and sets propagate in deterministic leaf-to-root order;
    noncomponent sources contribute directly to their associated root. Set
    publishes only its signed difference, exact descriptions remain distinct,
    and count queries expose local, description-specific, component, and root
    state.
21. Raised/dropped callbacks and retained trace records are source-to-root and
    monotonically sequenced. Callback exceptions are contained as
    `FSIM-UVM-OBJ-004`; a root transition from nonzero to zero records
    deterministic all-dropped detection after the final dropped event without
    taking Change 6 drain or all-dropped callback semantics early. Diagnostics
    `FSIM-UVM-OBJ-001` through `004` distinguish invalid ownership/lifetime,
    operation/association, bounded-state, and callback failure.
22. Runtime proof covers component and noncomponent sources, exact description
    identity, every local/propagated query, raise/drop/set differences, callback
    order and containment, monotonic traces, all-dropped detection, stale and
    cross-simulation handles, root/phase mismatch, negative and underflow
    counts, overflow, depth, source, description, entry, description-byte,
    callback-fanout, trace, and mutation ceilings with state-preserving
    rejection. The new header, implementation, and focused test are 236, 443,
    and 442 lines. The eight-worker exact-LLVM Debug tree rebuilt 140 affected
    steps warning-clean, then the final test expansion rebuilt two steps;
    `fsim.runtime`, `fsim.application`, `fsim.llvm`,
    `fsim.diagnostics-catalog`, `fsim.source-line-budget`, and
    `fsim.uvm-source-harness` pass 6/6 in 32.14 seconds before the documentation
    refresh. `git diff --check` is clean.
23. Change 6 is current. Implement per-object/per-phase drain time,
    cancellation/restart of pending drains, all-dropped callback sequencing,
    re-raise behavior during drains and callbacks, ready-to-end re-entry, and
    stable simulated-time scheduling with explicit pending-drain, callback,
    re-entry, delay, and teardown bounds.
24. Change 6 is complete in the same intentionally dirty worktree. The
    objection service can bind the simulation scheduler and owns exact
    source/phase drain settings plus cancelable, generation-qualified pending
    drain tasks. Transactional local/propagated drops remain immediate, while
    source drains gate all-dropped then ready-to-end notification until every
    pending source for the phase/root completes. Re-raise cancels pending work,
    a later final drop restarts the complete interval, zero drains finalize
    synchronously, and destruction cancels every bounded scheduled task.
25. All-dropped precedes ready-to-end, and a re-raise from either callback
    forces a later stable all-dropped/ready cycle. Runtime proof uses only
    scheduler ticks: two simultaneous drains in one root and an independent
    drain in a second root finish at exact times 3, 4, and 5; cancellation at
    tick 7 restarts a five-tick drain to tick 12. Coverage also includes exact
    source/phase setting identity, callback containment, missing-scheduler
    rejection, setting, pending, individual/aggregate delay, trace reservation,
    callback re-entry, and teardown bounds.
26. The objection header, implementation, and combined focused test are 336,
    750, and 738 lines. The final eight-worker exact-LLVM Debug build relinked
    20 affected targets warning-clean after the 29-step runtime rebuild;
    `fsim.runtime`, `fsim.application`, `fsim.llvm`,
    `fsim.diagnostics-catalog`, `fsim.source-line-budget`, and
    `fsim.uvm-source-harness` pass 6/6 in 33.67 seconds before the documentation
    refresh. `git diff --check` is clean.
27. Change 7 is complete in the intentionally dirty Batch 160 worktree. Task-
    phase completion now rejects while a participating root retains objection
    count or a pending drain. Successful completion records immutable final
    process snapshots, reclaims every live process record, and makes the former
    handles stale without breaking application inspection across the thirteen
    standard task phases and interpreter, compiled, or debug execution.
28. Scheduler-owned child processes retain exact phase/root/component, parent,
    depth, registration, and cancelable-task identity. A parent awaits every
    running descendant; nested children execute at stable simulated times, and
    scheduled exceptions are contained as `FSIM-UVM-PHASE-006` while their
    descendant waits are cancelled. Timeout, jump, root teardown, and service
    destruction cancel owned process trees and objection drains, prevent post-
    phase callbacks, and reclaim handles without disturbing caller-owned tasks,
    sibling roots, or an independently executing sibling domain.
29. Runtime proof covers objections/drains gating phase completion, immutable
    final snapshots, child/grandchild await, failure containment, timeout,
    jump, root teardown, destruction, stale handles, missing-scheduler, count,
    and depth rejection. Phase header, implementation, and test are 563, 1,905,
    and 1,616 lines. The eight-worker exact-LLVM Debug tree rebuilt 140 affected
    steps warning-clean, followed by 71 focused application/runtime steps and
    the remaining 75 full-tree steps after final snapshot integration;
    `fsim.runtime`, `fsim.application`, `fsim.llvm`,
    `fsim.diagnostics-catalog`, `fsim.source-line-budget`, and
    `fsim.uvm-source-harness` pass 6/6 in 32.37 seconds. `git diff --check` is
    clean.
30. Change 8 is complete in the intentionally dirty Batch 160 worktree. A
    bounded coordinator advances executing task phases only through the
    simulation scheduler, retries ready-to-end after objection/drain races, and
    returns explicit completed, stopped, or timed-out results with exact tick,
    callback, iteration, cancellation, and final execution evidence. The
    application-owned phase service now attaches to its interpreter scheduler
    after construction.
31. Missing scheduler work with live phase state diagnoses deadlock;
    ready-to-end re-entry, callback work, zero-time/delta stabilization, and
    outer iteration excess diagnose as `FSIM-UVM-PHASE-008` after phase-local
    cleanup. Timeout and stop cancel only phase-owned work, backward jump makes
    a stopped/timed-out interval restartable, and independent simulation owner
    tokens keep all process, objection, drain, and scheduled-event state
    isolated.
32. Runtime evidence covers a two-root ready objection race stabilized by a
    tick-3 drain, exact tick-5 timeout with caller-task survival, stop and clean
    restart, no-work deadlock, zero-time delta livelock, callback and ready
    re-entry ceilings, and an independently alive simulation completing at tick
    2 without retained state. Phase header/main/coordinator/test files are 591,
    1,929, 110, and 384 lines; application simulation is 1,901 lines. The final
    eight-worker exact-LLVM Debug continuation rebuilt 108 affected steps
    warning-clean; `fsim.runtime`, `fsim.application`, `fsim.llvm`,
    `fsim.diagnostics-catalog`, `fsim.source-line-budget`, and
    `fsim.uvm-source-harness` pass 6/6 in 32.93 seconds. `git diff --check` is
    clean.
33. Change 9 is complete in the intentionally dirty Batch 160 worktree. A
    simulation-owned TLM1 service provides owner-qualified port, export, and
    implementation handles; exact interface, nominal request/response profile,
    direction, component/root hierarchy, source declaration order, cardinality,
    fanout, chained-export resolution, stable binding revisions, and exact
    debug naming. Whole-graph resolution is transactional. The focused matrix
    rejects foreign/stale ownership, kind/interface/profile/direction/root
    mismatch, duplicates, cycles, missing required connections, and endpoint,
    fanout, depth, work, profile, and mutation ceilings through
    `FSIM-UVM-TLM1-001` through `004`. The eight-worker exact-LLVM Debug tree
    rebuilt 142 steps warning-clean. Runtime, application, LLVM, catalog,
    source-policy, and UVM harness gates pass 6/6 in 31.70 seconds; `git diff
    --check` is clean.
34. Change 10 is complete in the intentionally dirty Batch 160 worktree. The
    simulation-owned TLM1 service executes blocking and nonblocking put/get/
    peek/transport over bounded implementation FIFOs and transport handlers.
    Owner-qualified operation handles preserve request/response nominal types,
    packed/object payload identity, root/endpoint ownership, source-order
    reservations, deterministic feasible-waiter wake order, exact simulated
    completion time, and stable completed/cancelled snapshots. Master, slave,
    and bidirectional request/response roles, can/try behavior, provider FIFO
    access, capacity/payload/pending/execution ceilings, phase timeout/jump/
    completion/root teardown cancellation, and post-phase callback suppression
    are covered. A 96-bit nominal payload round trip passes in interpreter,
    LLVM compiled, and debug application contexts. `FSIM-UVM-TLM1-005` and
    `006` cover invalid execution and bounded-state failures. The eight-worker
    exact-LLVM Debug tree rebuilt 143 steps warning-clean; the main sources and
    tests remain below policy. Runtime, application, LLVM, catalog,
    source-policy, and UVM harness gates pass 6/6 in 31.14 seconds; `git diff
    --check` is clean.
35. Change 11 is complete in the intentionally dirty Batch 160 worktree.
    Analysis ports broadcast over frozen resolved target/callback snapshots in
    declaration order through chained exports and intentional fanout. Named
    macro-style implementations, subscribers, and bounded analysis FIFOs retain
    one publication sequence, independent value copies, shared live object
    handles, and exact nominal/root/endpoint identity. Subscriber/FIFO failures
    are contained while later targets continue; graph and callback mutations
    appear only after rebinding. Cross-root connections still reject. Callback,
    recursion, publication, failure, delivery, and queue ceilings are bounded
    through `FSIM-UVM-TLM1-007` and `008`. Runtime proof covers the positive and
    negative matrix; an exact 73-bit nominal broadcast passes in interpreter,
    LLVM compiled, and debug application contexts. The eight-worker exact-LLVM
    Debug tree rebuilt 140 steps warning-clean; sources remain below policy.
    Runtime, application, LLVM, catalog, source-policy, and UVM harness gates
    pass 6/6 in 30.41 seconds; `git diff --check` is clean.
36. Change 12 is complete in the intentionally dirty Batch 160 worktree. The
    simulation-owned TLM2 service binds generation-checked, root-qualified
    initiator, target, and both passthrough socket families through exact
    protocol, nominal payload/phase, bus-width, cardinality, cycle, and hop
    validation. Blocking transport, nonblocking forward/backward phase state,
    debug transport, DMI acquisition/invalidation, annotated delays,
    extensions, response status, cancellation, and immutable final transaction
    snapshots preserve exact owner identity and deterministic target resolution.
37. Runtime proof covers a three-hop passthrough chain, cross-root/profile/cycle
    rejection, payload and phase nominal mismatch, byte enables, streaming
    width, extensions, response, debug, DMI, forward/backward and custom phases,
    callback exception containment, cancellation, and payload, callback,
    outstanding-transaction, and hop ceilings. Application proof executes an
    exact 37-byte generic payload with extension, response, and tick-7
    completion in interpreter, LLVM compiled, and debug contexts.
    `FSIM-UVM-TLM2-001` through `005` distinguish invalid handles, socket
    graphs, payloads/DMI, callbacks/phases, and bounded resources. The TLM2
    header, structural source, transport source, focused test, and application
    simulation are 430, 363, 551, 498, and 1,908 lines. The eight-worker
    exact-LLVM Debug tree rebuilt 140 affected steps warning-clean; runtime,
    application, LLVM, catalog, source-policy, and UVM harness gates pass 6/6
    in 35.45 seconds. `git diff --check` is clean.
38. Change 13 is complete in the intentionally dirty Batch 160 worktree. Public
    `UvmDebugSnapshot` queries capture exact scheduler time/delta plus domain,
    phase, phase-process, objection, drain, TLM1 endpoint/FIFO/operation, and
    TLM2 socket/transaction state. Connections retain root-qualified debug
    names and payloads are owning copies. Record, aggregate payload-byte, and
    formatted-output ceilings reject through `FSIM-UVM-DEBUG-002`; generation,
    service-revision, and time/delta checks diagnose stale or advancing capture
    as `FSIM-UVM-DEBUG-001`. The application now owns and exposes its objection
    service alongside its phase and TLM services.
39. Debugger `uvm summary|phases|objections|tlm1|tlm2|all` commands use the same
    bounded snapshot. Domain-qualified phase breakpoints and `step phase` poll
    scheduler safe points without changing existing debugger behavior. Runtime
    proof covers exact objection/drain enumeration; all three application
    engines prove a two-domain, 21-phase, two-root, four-endpoint/one-FIFO, and
    two-socket snapshot, root-qualified formatting, bound rejection, and phase-
    breakpoint registration. The public header, debugger, inspection unit,
    application simulation, phase implementation, and objection implementation
    are 685, 1,826, 259, 1,912, 1,946, and 876 lines. The eight-worker exact-
    LLVM Debug tree rebuilt 75 affected steps warning-clean; runtime,
    application, LLVM, catalog, source-policy, and UVM harness gates pass 6/6
    in 31.63 seconds. `git diff --check` is clean.
40. Change 14 is complete in the intentionally dirty Batch 160 worktree. The
    simulation-owned `SystemVerilogUvmActivityService` publishes immutable,
    sequence- and scheduler-stamped graph, phase-state, objection, drain,
    connection, transaction, FIFO, and quiescence events through frozen token-
    ordered observer snapshots. Observer mutation affects only later events;
    re-entry and resources are bounded; callback exceptions, including a full
    retained-failure log, remain contained and do not alter scheduling. Public
    `Simulation` accessors/hooks expose the same root-qualified stream.
41. VCD tracing replays pre-attachment activity and then emits live sequence,
    kind, action, root, value, and stable identity/detail-hash transitions below
    `__fsim.uvm.activity`; trace teardown removes its observer before releasing
    storage. The backend-neutral stream is ready for the FST writer assigned to
    Batch 171 without implementing that encoder early. Runtime proof covers
    callback mutation/containment/bounds, objection/drain ordering, FIFO
    occupancy, and quiescence. All three application engines cover graph,
    phase, connection, transaction, and FIFO activity, while the CLI proves
    deterministic VCD names and scaling. `FSIM-UVM-ACTIVITY-001` and `002` are
    cataloged. The touched activity, phase, objection, TLM, application, and
    focused-test sources remain below policy at 125, 108, 1,990, 908, 532, 961,
    441, 605, 1,090, 1,928, 95, 788, 393, and 2,164 lines. The eight-worker exact-
    LLVM Debug tree rebuilt 79 affected steps warning-clean plus four focused
    test steps; runtime, application, LLVM, catalog, source-policy, and UVM
    harness gates pass 6/6 in 28.71 seconds. `git diff --check` is clean.
42. Change 15 is complete in the intentionally dirty Batch 160 worktree. The
    append-only `fsim_uvm_foreign_host_v1` table gives DPI and VPI integrations
    one stable C ABI without changing their frozen plug-in descriptors. Fixed-
    width layouts expose simulation-qualified snapshot generations, records,
    and activity callbacks. Identity/detail/payload sizes are queried before
    copying into caller-owned buffers; callback strings are borrowed only for
    the invocation. The simulation-owned foreign service captures phase/
    process, objection/drain, TLM1 endpoint/FIFO/operation, and TLM2 socket/
    transaction state as bounded owning records.
43. Release makes foreign generations stale, cross-service identities reject,
    and snapshot/record/text/payload/callback/buffer ceilings preserve prior
    state. Callback status failures remain contained by the activity service;
    teardown removes callbacks before foreign storage disappears. A C unit
    freezes the ABI layouts. All three engines prove two-root isolation, exact
    37-byte TLM2 payload copies, TLM1 FIFO state, sizing, cross-simulation and
    stale rejection, snapshot exhaustion, transaction cancellation, callback
    failure containment, teardown, and fresh simulation identities.
    `FSIM-UVM-FOREIGN-001` and `002` are cataloged. The ABI/C++ headers,
    implementation, application header/simulation, C probe, and application
    matrix are 122, 103, 382, 698, 1,932, 17, and 2,346 lines. The eight-worker
    exact-LLVM Debug tree rebuilt 77 affected steps warning-clean; all six gates
    pass in 28.18 seconds, and `git diff --check` is clean.
44. Change 16 is complete in the intentionally dirty Batch 160 worktree. A
    bounded schema-1 `SystemVerilogUvmCheckpoint` owns the exact scheduler
    time/delta, phase graph/state, objections/drains, TLM1 endpoint/operation/
    FIFO state and payloads, TLM2 socket/transaction state and payloads, and
    content/cache/design/root provenance. Foreign records now encode the exact
    portable phase edges and TLM metadata needed by that checkpoint. Host
    callbacks and live phase processes remain nonportable and are summarized
    only by fixed-width counts; no pointer or callback enters serialized state.
45. Design format 3 requires the `sv-uvm` payload at `state/sv-uvm.bin`, framed
    by `FSIMUVM1`, while `.fsimobj` retains the corresponding portable class and
    source definitions. Publication creates a deterministic standard 21-phase
    bootstrap state. Loading validates schema, foreign ABI, provenance,
    structure, checksum, and the exact clean-restart baseline before simulation.
    Runtime and application proof cover byte-stable repetition, future schema,
    ABI/provenance/root/state mismatch, truncation, physical corruption,
    nonportable records, relocation between aliased roots, isolated cold/warm
    caches, clean restart, and exact interpreter/LLVM compiled/debug live-state
    equality. `FSIM-UVM-STATE-001` and `002` are cataloged.
46. The checkpoint header/source, foreign header/source, TLM1 header/execution,
    application header/codec/design/simulation, runtime test, artifact test,
    class matrix, and two focused case wrappers are 121, 245, 106, 548, 534,
    970, 705, 952, 940, 1,942, 159, 915, 2,396, 11, and 11 lines. The exact-LLVM
    Debug tree completed a full 685-step warning-clean rebuild; the later design-
    format bump completed its 22-step incremental rebuild warning-clean. The
    final no-op eight-worker build is clean, all six runtime/application/LLVM/
    catalog/source-policy/UVM-harness gates pass in 32.00 seconds, and `git diff
    --check` is clean.
47. Change 17 is complete in the intentionally dirty Batch 160 worktree. One
    source fixture imports exact, unmodified UVM 1.2 and UVM 2020-3.1 object,
    component, phase, blocking-put-port, and FIFO types. Its derived constructor
    and real build/connect/end-of-elaboration/start-of-simulation/run/extract/
    check/report/final callbacks execute for two aliased roots. An explicit
    intrinsic boundary materializes upstream `uvm_pkg` base constructors through
    simulation-owned services instead of bootstrapping a competing upstream
    scheduler/report/resource/component hierarchy; the user constructor still
    executes and proves marker `10`. A public run-phase continuation preserves
    suspended host/DPI/SystemC work until objection `1/0` and a three-tick drain
    settle.
48. Direct source, one portable object, relocated O0/O2 portable designs,
    interpreter, compiled, debug, callbacks, isolated cold/warm native caches,
    and activity traces produce the exact transcript
    `FSIM-UVM-PHASE-TLM-PASS phases=build/connect/eoe/sos/run/extract/check/report/final roots=left,right objection=1/0 drain=3 payload=37 result=42 source=37/42/1`.
    Cold compiled runs assert zero hits plus nonzero misses/stores; warm runs
    assert nonzero hits and zero misses. All fourteen direct/O0/O2 cold/warm/
    debug traces are 7,062 bytes with SHA-256 `4546c05e2625a4f934cbf1e30e3786e78b2abed76871a32434b0454eaac71853`.
    The `.fst`-designated outputs use the backend-neutral VCD activity stream;
    binary FST encoding remains locked to Batch 171. The retained evidence roots
    are `/tmp/fsim-batch160-change17-real-source-surface-final-uvm12` and
    `/tmp/fsim-batch160-change17-real-source-surface-uvm2020`.
49. Stage-isolated capped measurements keep UVM 1.2/2020.3.1 direct analysis at
    4,242,276/4,735,592 KiB RSS under 6 GiB, compile at
    3,308,520/3,672,868 KiB under 5/6 GiB, O0/O2 elaboration at
    3,198,008/3,198,256 and 3,441,912/3,441,688 KiB under 5/5.5 GiB, and all
    execution below 1,000,000 KiB under 3 GiB. The final 104-step eight-worker
    exact-LLVM Debug rebuild is warning-clean; all six runtime/application/LLVM/
    catalog/source-policy/UVM-harness gates pass in 32.27 seconds, and dedicated
    UVM tests `fsim.application.uvm_phase_tlm.1_2` and `.2020_3_1` are registered.
    The application header/simulation/source-method/phase bridge, fixture,
    focused test, and runner are 709, 1,943, 652, 114, 82, 499, and 47 lines.
50. Change 18 is complete in the same intentionally dirty Batch 160 worktree.
    `fsim.uvm-phase-tlm-matrix` maps all 33 phase, objection, TLM1, TLM2,
    debugger, activity, foreign, and checkpoint codes to executable runtime or
    application evidence. The consolidated positive contract covers graph
    construction, jumps, synchronization, callbacks, objection/drain bounds,
    process cancellation, TLM1 FIFO/transport/analysis, TLM2 sockets/payloads/
    DMI, artifact relocation/restart, observer containment, and every governed
    resource ceiling. Exact diagnostic assertions now include contained TLM1
    failures, cross-service/released TLM2 sockets, stale/cross-owner foreign
    handles, stale debug snapshots, and checkpoint record/text/root/identity/
    payload/process/callback limits.
51. The exact two-root fixture adds a ready-to-end objection race that drains
    deterministically at tick 5 and a suspended-process deadlock that rejects
    with `FSIM-UVM-PHASE-008` and cleans every process and objection. Unmodified
    UVM 1.2 and UVM 2020-3.1 pass direct source, portable compile, O0/O2
    elaboration, compiled cold/warm cache, and debug execution in 367.21 and
    481.55 seconds. All fourteen final traces are 10,357 bytes with SHA-256
    `f78d9f125531a9d4764e6c5336e0bfdf8d9893577932f1cbb0d198355fb7c7ac`.
52. The eight-worker exact-LLVM Debug build completes 77 affected steps
    warning-clean. Catalog, aggregate-matrix, source-policy, UVM-harness,
    application-class, and runtime gates pass 6/6 in 7.65 seconds. The
    checkpoint header/source, foreign source, checkpoint/analysis/TLM2 runtime
    tests, application class/exact matrices, aggregate contract, and runner are
    120, 267, 556, 193, 322, 462, 2,302, 565, 130, and 47 lines; `git diff
    --check` is clean.
53. Change 19 is current. Synchronize the public UVM guide, exact example,
    architecture, language support, diagnostics, feature/conformance evidence,
    source provenance, measured resource baselines, test inventory, release
    audits, and restart handoff. Pass documentation, catalog, inventory,
    installation, portability, and release-candidate contracts. Preserve all
    accumulated Changes 1-18 files; do not reset, commit, push, run the
    sanitizer, or inspect hosted CI before Change 20.
54. Change 19 is complete in the same accumulated dirty worktree. README, the
    public UVM guide and exact transcript, architecture, language support,
    diagnostics, governed source provenance, measured resource baselines,
    feature evidence, inventories, release audits, candidate corpus, and this
    handoff now state the exact bounded Batch 160 phase/objection/TLM contract.
    `SV-801` through `SV-810` advance the matrix to 1,250 executable rows and
    5,000 evidence cells across 545 paths: 237 test, 286 production, and 22
    release paths with 135 runtime owners. Matrix digest
    `25300a46781d8945f884358d4fabf05489415cbbee97405edffee7e4a0be4943`
    and evidence digest
    `49f1bb1b659bf6e63aa95402f9e0e302330647d0efa8d9c08f6d7cb1be2d62ba`
    are frozen in the release contracts.
55. The synchronized inventories cover 2,023 production diagnostics, 769
    bounded C/C++ sources, 870 SPDX-owned authored files, and 285 authored
    test/control files. The upstream UVM trees remain exact external build
    products and are excluded from authored inventories. The main Debug tree
    registers 118 tests; the governed-source configuration adds the two exact
    unmodified-UVM phase/TLM tests.
56. The complete documentation, catalog, UVM matrix, conformance, release,
    inventory, installed-public, and Linux/Windows portability prefix passes
    28/28 in 17.87 seconds; `git diff --check` is clean. Preserve Changes 1-19
    and begin Change 20 with the scheduled LLVM-disabled sanitizer, fresh
    exact-LLVM Debug and Release eight-worker builds, all regressions and final
    resource audits, then one commit/push and mandatory non-documentation hosted
    CI inspection/repair. Do not commit or push before those local gates pass.
57. Change 20 local qualification is complete in the accumulated dirty Batch
    160 worktree. The fresh LLVM-disabled GCC 13.3 ASan/UBSan build completes
    warning-clean with eight workers. Its first run passed 116/117 and exposed
    a stale VITAL relocation fixture: format-3 `.fsimdesign` requires
    `state/sv-uvm.bin`, but that test copied only the six earlier state
    payloads. The fixture now copies all seven. Its isolated rerun passes in
    5.13 seconds and the exact final sanitizer regression passes 117/117 in
    1,180.07 seconds with leak detection disabled and ASan/UBSan halt-on-error.
58. Fresh exact-LLVM 22.1.8 Debug and Release trees each complete 679-step
    warning-clean eight-worker builds. Debug passes 117/117 in 381.93 seconds
    and Release passes 117/117 in 314.14 seconds. Both complete suites include
    source, catalog, UVM matrix, inventory, installed-public, resource,
    portability, differential, and release-candidate gates. The sanitizer,
    Debug, and Release trees are all no-op under eight workers; their final logs
    contain no sanitizer, runtime-error, or CTest-failure markers.
59. The governed-source tree completes a 338-step warning-clean eight-worker
    rebuild and is also no-op. Its exact final upstream tests pass 2/2 in
    856.23 seconds: UVM 1.2 in 369.34 seconds and UVM 2020-3.1 in 486.87
    seconds. The authoritative current traces are retained below
    `build/llvm22-ninja-debug-uvm/uvm-phase-tlm-example`; all fourteen direct,
    O0/O2 cold/warm, and debug traces are exactly 10,357 bytes with SHA-256
    `f78d9f125531a9d4764e6c5336e0bfdf8d9893577932f1cbb0d198355fb7c7ac`.
    The older `/tmp/fsim-batch160-change17-real-source-surface-*` roots remain
    historical Change 17 evidence and intentionally retain their earlier
    7,062-byte direct traces.
60. Next action: run final diff/status checks, commit and push the single Batch
    160 implementation, then inspect and repair every non-documentation hosted
    Linux, Windows, sanitizer, and fuzz job until green. After hosted closeout,
    save and push the exact Batch 161 restart plan and clear context before any
    Batch 161 implementation.
61. The Batch 160 implementation is commit
    `2b5810303c7f8e39f9846955d95871ada5bfcd2c`. Hosted portability repairs are
    `95d3bd6`, `75f5d47`, `a7a96eb`, `73313eb`, `97d6378`, `7e70919`,
    `534d31c`, `a94bc9e`, `7434520`, and `bc2a8ed`. Hosted run `31286177825`
    then exposed a 44-byte malformed-SystemVerilog parser OOM and invalidated
    class-heap references during recursive UVM clone on Windows.
62. Repair `2158b2cd484b9b06f66621d2d3dca5eabb9cb322` adds progress invariants to
    module/package parsing, an exact 43-byte source regression, allocation-safe
    recursive UVM copy state, and a forced class-heap reallocation regression.
    The exact fuzz input falls from `std::bad_alloc` at 1,925,644 KiB RSS to a
    0.01-second pass at 33,424 KiB; deterministic 20,000-run fuzz passes at
    43,232 KiB maximum RSS. Focused frontend/runtime Debug and Release,
    sanitizer, 20 repeated VHDL-composite runs, direct clang-cl `/W4 /WX`, all
    twelve focused policy gates, and both full exact-LLVM eight-worker builds
    pass.
63. Replacement hosted run `31304022606` passes the fuzz job, all four Ubuntu
    jobs, both Windows clang-cl jobs, ordinary Windows MSVC Debug and Release,
    and Windows MSVC/LLVM Release. Windows MSVC/LLVM Debug passes tests 1-75 of
    118 before GitHub cancels the still-running `fsim.application.sv_containers`
    test at the 70-minute job ceiling; its log records no failed test. The user
    explicitly accepted this result on 2026-08-09 and directed advancement.
64. This pre-Batch-161 checkpoint raises only the `windows-llvm22` workflow
    ceiling from 70 to 120 minutes, preserving the test suite and every bounded
    per-test limit. Commit and push this planning/CI-budget checkpoint, verify a
    clean synchronized branch, do not monitor its resulting workflow, clear
    context, and resume at Batch 161 Change 1 from the checkpoint at the top of
    this file.

## Batch 159 active checkpoint - 2026-08-07

1. Start in `/home/colin/projects/fsim`, read this file and the authoritative
   expanded Batch 159 allocation in `implementation_plan_v2.md`, and verify
   branch `codex/v2` is clean and synchronized at Batch 158 closeout
   `5aa0e6e7a205fe91ebe7ce2b34fbe3c890cd10fc` plus this documentation-only
   Batch 159 planning checkpoint.
2. This checkpoint implements the required pre-batch flow. Batch 159 has been
   expanded into twenty exact changes without broadening the locked UVM object,
   factory, configuration/resource, and reporting foundation. No Batch 159
   implementation file has been changed; clear context after pushing this plan
   and resume only from this section and the authoritative allocation.
3. Preserve the completed VHPI implementation and every frozen Batch 158
   baseline. Batch 158 passed exact-LLVM 22.1.8 Debug and Release 114/114 in
   368.78 and 312.67 seconds, respectively, and is pushed as `5aa0e6e`.
4. Change 1 begins only with a governed external-source harness for unmodified
   UVM 1.2 and UVM 2020-3.1 package/macro entry points. Record exact upstream
   identity and checksums, keep all fetched/generated/build state outside the
   authored source inventory, use isolated work/cache roots, and do not vendor
   or patch either standard library.
5. Treat initial UVM analysis failures as evidence to classify, not permission
   to add compatibility caps or source rewrites. Close only the language and
   runtime prerequisites assigned to Changes 2-4, with stable cataloged
   negatives and focused tests before advancing to executable UVM objects.
6. Accumulate Changes 1-20 in one dirty Batch 159 worktree. Use at least eight
   workers for local builds, retain exact test output, and update this handoff
   after every completed change. Do not reset, commit, or push the
   implementation before Change 20.
7. Batch 159 is not a sanitizer or hosted-CI monitoring boundary. Do not run a
   sanitizer or inspect hosted CI. Change 20 owns the full non-sanitized
   exact-LLVM Debug/Release regressions and the single implementation commit and
   push.
8. Change 1 is complete in the dirty worktree. `FsimUvmSources.cmake` defines
   one governed schema and two immutable release records. UVM 1.2 retains its
   official Accellera June 2014 archive identity; UVM 2020.3.1 additionally
   records official release commit
   `78c06547a2a0a29b3dc9dcafae62b75b2ff61544`. Both distributions remain
   external build products and are never copied into `third_party` or the
   authored source inventory.
9. The UVM 1.2 archive SHA-256 is
   `502a2e605ce552bfd9767803c7e99a053715b00f7a9c4c511c3fbfddfb30157c`;
   its 960-file extracted-tree SHA-256 is
   `badb7104548cabd934c6ca95dd126a3b6ee3c71ff6974acffdd4e63d2bfd1f49`.
   UVM 2020.3.1 uses archive SHA-256
   `0d6a2ca5811c787e5aa1e945abaaaa5d5c295148d5e704c9fa910d7b288cbcf7`
   and 326-file tree SHA-256
   `0d0c409af4ba5984df5a3e7d4730289183fa5b714b019b712c9f01e3f769f980`.
10. Default `OFF` configuration performs no UVM network access. Fresh `FETCH`
    and predownloaded `ARCHIVE` materializations both pass end-to-end in
    isolated work roots, validate every file plus twelve exact package, macro,
    DPI, license, notice, and README entries, and emit complete generated
    manifests. An authored-source work root rejects before download or
    extraction. The registered offline `fsim.uvm-source-harness` CTest passes
    in 0.02 seconds, the source-policy gate retains 703 bounded C/C++ sources,
    the eight-worker exact-LLVM Debug build has no work, and whitespace checks
    are clean.
11. Preserve the accumulated Change 1 worktree and begin Change 2 by running
    both exact upstream macro entry points through the current preprocessor.
    Classify every first failure, then close only include-guard, token
    composition/stringification, variadic forwarding, nested expansion, and
    generated-declaration gaps with cataloged malformed-expansion evidence.
    Do not commit, push, run a sanitizer, or inspect hosted CI before Change 20.
12. Change 2 is complete in the same dirty worktree. The lexer preserves a
    comment-ending continuation long enough for the preprocessor to classify
    it, while directive collection remains limited to consecutive physical
    lines and continuation markers remain preprocessing trivia. This closes
    the UVM 1.2 multiline-comment boundary without swallowing later macro
    definitions or exposing continuation tokens to the parser.
13. Macro expansion now selects nested `ifdef`/`ifndef`/`elsif`/`else`/`endif`
    replacement bodies whether multiline or inline, expands forwarded
    arguments before substitution, preserves balanced
    parenthesized/bracketed/braced tuples, treats paste markers as either token
    composition or punctuation delimiters, and composes special quoted strings
    from literal text, parameters, paste markers, and nested object macros.
    Include guards, nested suffixes, generated modules, tuple forwarding,
    punctuation-delimited member selection, and both conditional branches have
    hermetic positive evidence. `FSIM-SV-PP-049` through `051` freeze
    malformed/unmatched and unterminated replacement conditionals plus
    malformed special strings.
14. Offline ARCHIVE mode rebuilt the frontend test with both exact source roots
    and passed the complete frontend executable in 59.72 seconds while
    preprocessing both complete unmodified UVM 1.2 and UVM 2020-3.1 packages
    plus representative `uvm_macros.svh`, `uvm_object_utils`, and
    `uvm_analysis_imp_decl` expansions. The cache is restored to default
    `OFF`. The full incremental exact-LLVM Debug tree then rebuilt 45 affected
    steps warning-clean with eight workers; `fsim.frontend`,
    `fsim.diagnostics-catalog`, `fsim.source-line-budget`, and
    `fsim.uvm-source-harness` pass 4/4 in 0.40 seconds, and `git diff --check`
    is clean. Preserve Changes 1-2 and begin Change 3 at the package/class/type
    boundary; do not commit, push, run a sanitizer, or inspect hosted CI.
15. Change 3 is complete in the same dirty worktree. Class parameter lists now
    accept shorthand and mixed declarations; named enums accept explicit bases
    and unsized based literals; package variables retain initializers and an
    explicit `const` flag; unpacked typedef dimensions no longer reject; and
    class localparams retain static/const ownership plus their initializer.
    Qualified out-of-block constructors and package-owned method definitions,
    parameterized static calls, string-valued type/specialization actuals,
    qualified function/task end labels, and `const ref` formals all have
    UVM-shaped positive evidence.
16. SystemVerilog `Type` now retains every source-ordered packed dimension.
    Concrete dimensions flatten with checked multiplication while symbolic
    class-parameter bounds survive until specialization; exact dimension
    expressions participate in type identity, qualified-name scanning, and
    base-class actual specialization. A UVM-shaped
    `bit [7:0][N-1:0]` resource-wrapper base actual retains both dimensions.
17. Portable and application artifact codecs archive the new packed-dimension
    and package-const fields symmetrically. Portable-unit schema 12, portable
    artifact schema 8, runtime-state schema 18, and class-state schema 8 are
    frozen by static assertions. An explicit portable round trip proves a const
    declaration with two packed dimensions reloads with width 32.
18. Parser-owned procedural type tracking prevents a known class-handle formal
    from entering name-only built-in string/container arity checks. The
    UVM-shaped two-argument `uvm_object::compare` regression parses and resolves
    through class overload selection, and the genuine UVM 1.2
    `FSIM-SV-SEM-127` diagnostic at `lhs.compare(rhs, this)` is gone.
19. The eight-worker exact-LLVM Debug frontend and CLI build is warning-clean;
    `fsim_frontend_tests`, `fsim_library_tests`, and
    `fsim_application_tests core` pass. The application gate includes artifact
    phase semantics, relocated portable/runtime state, interpreter and compiled
    simulation, and class integration. `git diff --check` is clean.
20. Governed scans of both unmodified UVM packages no longer expose the
    Change 3 class/type failures at their original sites. Remaining sampled
    `UNSUPPORTED-045`/`PARSE-260` class members are named events, and later UVM
    2020 register-sequence cascades begin with procedural macro expansion in a
    case-item context (`PARSE-048`/`UNSUPPORTED-008`) before any misleading
    end-label or package-item fallout. Preserve this accumulated Changes 1-3
    worktree and begin Change 4 with virtual interfaces, process/event and
    semaphore/mailbox ownership, command-line/DPI prerequisites, and clean
    package analysis/elaboration. Do not commit, push, run a sanitizer, or
    inspect hosted CI before Change 20.
21. Change 4 is complete in the same dirty worktree. Virtual-interface views,
    named events, process handles, semaphore/mailbox construction, procedural
    `for`/`foreach` forms, multiline formatted output, command-line queries,
    DPI declarations, class inheritance/aliases, qualified and omitted-
    parentheses calls, and the remaining UVM package expression surface now
    parse and resolve without compatibility source edits. Both exact upstream
    `uvm_pkg.sv` entry points report one checked design unit and no diagnostic.
22. The reported greater-than-20-GiB `fsim-sv` growth was traced with bounded
    RSS runs and debugger phase sampling. Parsing completed below the cap; the
    growth began in class-expression resolution because method-body hydration
    copied every reachable class method body into every call site, recursively
    materializing the UVM call graph as a tree. Hydration now starts only from
    executable design-unit functions, tasks, and processes, expands each
    canonical class method at most once per root, and retains profile-only call
    sites for recursion and repeated calls. Focused evidence proves executable
    roots still receive one callable body while class declarations never own
    recursively expanded copies.
23. Final UVM 1.2 analysis under a 3-GiB address-space ceiling exits zero in
    47.86 seconds at 1,689,236 KiB maximum RSS. Final unmodified UVM 2020-3.1
    analysis under the same ceiling exits zero in 61.12 seconds at 1,936,024
    KiB maximum RSS. Retain `/tmp/fsim-uvm12-change4-final.log`,
    `/tmp/fsim-uvm12-change4-final-time.txt`,
    `/tmp/fsim-uvm2020-change4-final2.log`, and
    `/tmp/fsim-uvm2020-change4-final2-time.txt` as the exact local evidence.
24. Change 4 also restores two older contracts exposed by the wider gate: an
    untyped value parameter keeps the `implicit` marker so a 129-bit unsized
    decimal remains self-determined rather than being truncated through
    explicit `int`, and an undeclared named-event trigger never creates an
    implicit net. The complete frontend and elaboration executables pass after
    both corrections. The incremental exact-LLVM Debug tree rebuilt 171
    affected steps and then 17 final relinks warning-clean with eight workers;
    the final focused frontend, library artifact, HIR, elaboration/container,
    core application, virtual-interface, named-event, fork, synchronization,
    container, diagnostics-catalog, source-line-budget, and UVM-source-harness
    set passes 14/14 in 136.11 seconds.
25. Preserve accumulated Changes 1-4 and begin Change 5 with executable
    `uvm_object` construction, naming/type identity, clone/copy/compare,
    printing/recording hooks, field automation, and deterministic recursive-
    object handling. Do not reset, commit, push, run a sanitizer, or inspect
    hosted CI before Change 20.
26. Change 5 is complete in the same dirty worktree. A simulation-owned
    `SystemVerilogUvmObjectService` registers exact class specializations and
    descriptor-ordered fields, assigns monotonic instance IDs, and owns names,
    full names, and stable type identity over generation-qualified class-heap
    handles. Deep clone/copy preserves cycles and shared aliases while explicit
    reference fields remain aliases; bidirectional compare traversal rejects
    alias-shape mismatches. `NoCopy`, `NoCompare`, `NoPrint`, and `NoRecord`
    automation flags plus `do_copy`, `do_compare`, `do_print`, and `do_record`
    hooks are executable. Depth, object, field, and output limits terminate
    excessive graphs, and failed clone/copy rolls back every created or changed
    object transactionally.
27. Every elaborated class derived from exact `uvm_object` is registered and
    initialized on source-level `new`. Canonical base `clone`, `copy`,
    `compare`, `print`, `record`, and `get_inst_id` calls cross the native class
    boundary without capturing derived overrides. The application fixture
    constructs recursive UVM-shaped objects and proves source-level clone,
    copy, compare, print, and record behavior through interpreter, compiled,
    and debug engines; runtime evidence covers naming/type identity, instance
    IDs, cycles, aliases, field flags, hooks, mismatches, and resource-failure
    rollback.
28. The complete exact-LLVM Debug graph rebuilds 77 affected steps
    warning-clean with eight workers. The SystemVerilog-HIR, frontend, portable
    library artifact, diagnostic-catalog, source-line-budget, UVM harness,
    elaboration, application, and runtime gates pass 9/9 in 27.47 seconds, and
    `git diff --check` is clean. Final UVM 1.2 analysis under a 3-GiB
    address-space ceiling exits zero in 55.27 seconds at 1,688,064 KiB maximum
    RSS; final unmodified UVM 2020-3.1 analysis exits zero under the same cap in
    68.55 seconds at 1,935,212 KiB. Retain
    `/tmp/fsim-uvm12-change5-final.log`,
    `/tmp/fsim-uvm12-change5-final-time.txt`,
    `/tmp/fsim-uvm2020-change5-final.log`, and
    `/tmp/fsim-uvm2020-change5-final-time.txt` as the exact local evidence.
29. Preserve accumulated Changes 1-5 and begin Change 6 with executable
    `uvm_component` construction and parent/child hierarchy, full-name lookup,
    top-level ownership, deterministic traversal, duplicate rejection, and
    destruction/lifecycle behavior across multiple roots. Do not reset,
    commit, push, run a sanitizer, or inspect hosted CI before Change 20.
30. Change 6 is complete in the same dirty worktree. A simulation-owned
    `SystemVerilogUvmComponentService` provides isolated, identity-checked root
    contexts; unique sibling and top names; creation-ordered children and tops;
    exact relative, absolute, and root-scoped lookup; and generation-safe
    parent/root/full-name snapshots. Checked root, component, depth, child,
    name, and path budgets reject excessive work before attachment. Failed
    construction rolls back heap/object ownership and newly created automatic
    roots, while iterative postorder teardown invokes pre/post hooks leaf first,
    contains hook exceptions until all descendants are reclaimed, and destroys
    all remaining roots with the simulation.
31. `ClassAllocate` now carries an aligned packed-or-string kind vector, so
    source `new("name", parent)` preserves string actuals across reference,
    compiled, and debug execution without narrowing them into packed values.
    Validation and native-cache identities include the new shape. Exact
    `uvm_component` descendants initialize after their source constructor,
    empty names receive deterministic `COMP_<instance-id>` names, canonical
    `get_parent` and `get_num_children` cross the native method boundary,
    and inherited component `clone` returns null. Public application APIs
    create roots/components and expose the hierarchy service.
32. Runtime evidence covers multiple roots with equal top names, exact
    traversal/lookup, duplicates, cross-root parents, invalid names, depth,
    caller-owned rejection cleanup, stale handles, leaf-first lifecycle order,
    and root-isolated destruction. The application fixture covers two API
    roots, duplicate rollback, and source top/child construction through all
    three engines. The eight-worker focused targets build warning-clean; final
    application, runtime, LLVM, source-line-budget, and UVM-source-harness gates
    pass 5/5 in 26.45 seconds. The separately measured application gate peaks
    at 324,048 KiB RSS.
33. Final unmodified UVM 1.2 analysis under a 3-GiB address-space ceiling exits
    zero in 46.48 seconds at 1,687,704 KiB maximum RSS. Final unmodified UVM
    2020-3.1 analysis exits zero under the same ceiling in 57.88 seconds at
    1,934,580 KiB maximum RSS. Retain
    `/tmp/fsim-uvm12-change6-final.log`,
    `/tmp/fsim-uvm12-change6-final-time.txt`,
    `/tmp/fsim-uvm2020-change6-final.log`, and
    `/tmp/fsim-uvm2020-change6-final-time.txt` as the exact local evidence.
34. Preserve accumulated Changes 1-6 and begin Change 7 with type/object
    wrappers and object/component registry macro families, including
    parameterized registrations, stable type names, create-by-type/name, and
    duplicate or mismatched registration diagnostics. Do not reset, commit,
    push, run a sanitizer, or inspect hosted CI before Change 20.
35. Change 7 is complete in the same dirty worktree. A simulation-owned
    `SystemVerilogUvmRegistryService` assigns opaque monotonic wrapper handles,
    preserves source registration order, and indexes exact specialization,
    declaration, and stable type-name identities. Object and component kinds
    are distinct; parameterized names retain ordered actuals; ambiguous
    declarations, duplicate specialization or type names, unknown and stale
    wrappers, wrong-kind creation, callback specialization mismatches, and
    configured type/identity/name limits all reject before publishing invalid
    state. Failed callbacks and mismatched returns roll heap, object, and
    component ownership back transactionally.
36. Application registration recognizes the static `get_type` plus virtual
    `get_object_type` surface emitted by `uvm_object_utils`,
    `uvm_object_param_utils`, and `uvm_component_utils` families. Static and
    instance calls return identical wrappers through interpreter, compiled,
    and debug engines. Public create-by-type and create-by-name paths construct
    named objects and parented components; default parameterized specialization
    `UvmParamItem#(WIDTH=8)` retains a distinct stable name and creates by name.
    The registry-specific discovery/registration owner is isolated in
    `application_uvm_registry.cpp` after the source-line gate identified growth
    in `application_simulation.cpp`.
37. The full incremental exact-LLVM Debug tree rebuilt 149 affected steps and
    the final application relink warning-clean with eight workers. The runtime,
    application, LLVM, diagnostic-catalog, source-line-budget, and UVM source
    harness gates pass 6/6 in 28.16 seconds; the application gate alone passes
    all three engines in 24.87 seconds. `git diff --check` is clean.
38. Final unmodified UVM 1.2 analysis under a 3-GiB address-space ceiling exits
    zero in 48.88 seconds at 1,688,264 KiB maximum RSS. Final unmodified UVM
    2020-3.1 analysis exits zero under the same ceiling in 64.10 seconds at
    1,935,000 KiB maximum RSS. Retain
    `/tmp/fsim-uvm12-change7-final.log`,
    `/tmp/fsim-uvm12-change7-final-time.txt`,
    `/tmp/fsim-uvm2020-change7-final.log`, and
    `/tmp/fsim-uvm2020-change7-final-time.txt` as the exact local evidence.
    Preserve accumulated Changes 1-7 and begin Change 8 with factory type and
    instance overrides, wildcard instance paths, precedence and replacement
    rules, recursive-loop rejection, debug traces, and deterministic override
    reports. Do not reset, commit, push, run a sanitizer, or inspect hosted CI
    before Change 20.
39. Change 8 is complete in the same dirty worktree. A simulation-owned
    `SystemVerilogUvmFactoryService` stores bounded source-ordered type and
    instance overrides over Change 7 wrapper handles. Instance matches take
    precedence over type overrides; first registered instance match wins;
    linear bounded `*`/`?` matching covers full instance paths and deferred
    name-based originals; recursive chaining continues until a final registered
    wrapper is selected. Type replacement honors `replace`, exact duplicate
    instance entries are ignored, and registered component/object kinds remain
    enforced by the registry creation boundary.
40. Resolution records deterministic selected-step traces and increments use
    counts only after the complete chain succeeds. Self/recursive loops,
    unknown or stale targets, excessive depth, type/instance override counts,
    type-name/path lengths, and report bytes reject without partial count or
    creation state. Nonmutating debug resolution exposes the same selected
    chain, and stable reports list registered types plus type/instance entries,
    registration order, paths, targets, and use counts.
41. Public application access owns the factory beside the registry. Source
    `uvm_factory`-shaped `set_type_override_by_type`,
    `find_override_by_type`, `create_object_by_type`, and `print` calls cross a
    dedicated native boundary. The application differential proves source
    type override resolution plus creation, and API evidence proves a distinct
    matching instance override wins over a type override and creates its
    selected object through interpreter, compiled, and debug engines. Runtime
    evidence additionally covers name/type registration, wildcard paths,
    chained precedence, replace false/true, duplicate instances, object and
    component creation, loops, nonmutating debug traces, reports, and resource
    ceilings.
42. The full incremental exact-LLVM Debug tree rebuilt 75 affected steps
    warning-clean with eight workers. Runtime, application, LLVM,
    diagnostic-catalog, source-line-budget, and UVM source harness gates pass
    6/6 in 28.76 seconds; the application gate passes all three engines in
    25.59 seconds. `git diff --check` is clean. Final UVM 1.2 analysis under a
    3-GiB address-space ceiling exits zero in 49.25 seconds at 1,687,960 KiB
    maximum RSS; final UVM 2020-3.1 analysis exits zero under the same ceiling
    in 63.14 seconds at 1,935,164 KiB maximum RSS. Retain
    `/tmp/fsim-uvm12-change8-final.log`,
    `/tmp/fsim-uvm12-change8-final-time.txt`,
    `/tmp/fsim-uvm2020-change8-final.log`, and
    `/tmp/fsim-uvm2020-change8-final-time.txt` as exact local evidence.
    Preserve accumulated Changes 1-8 and begin Change 9 with typed resource-pool
    insertion, lookup, read/write, priority, auditing, callbacks, spell
    checking, and safe resource ownership. Do not reset, commit, push, run a
    sanitizer, or inspect hosted CI before Change 20.
43. Batch 159 Change 9 is complete in the same accumulated dirty worktree. A
    simulation-owned resource pool stores nominal packed, real, string, and
    generation-checked object values behind monotonic opaque handles. Resource
    records, callback lists, lookup results, audit history, packed widths,
    strings, names, scopes, accessors, spelling candidates/distance, and reports
    all have explicit limits. Resource erasure owns only metadata and callbacks;
    it never destroys an object-valued caller-owned heap object.
44. Exact name/type lookup applies bounded linear `*`/`?` scope matching, then
    orders matches by descending precedence, mutable high/low queue priority,
    and stable creation order. Typed reads and writes revalidate live object
    handles, publish revision/read/write counters only after success, and reject
    nominal mismatches or read-only writes without value mutation. Snapshot
    callback dispatch safely permits removal during invocation; callback
    exceptions are contained and recorded beside read, write, and rejected-write
    events in a fixed-size oldest-first audit ring. Bounded edit-distance spell
    checking and deterministic reports expose pool state without unbounded
    retained query structures.
45. `Simulation` owns the resource pool beside the Change 7 registry and Change
    8 factory and exposes mutable/const application access. The application
    differential inserts, scope-resolves, writes, reads, audits, and checks a
    typed packed resource through interpreter, compiled, and debug engines.
    Focused runtime evidence additionally covers precedence/priority, exact and
    wildcard scopes, type lookup, callback removal and exception containment,
    audit eviction, spelling, nominal/read-only negatives, report/resource/
    callback ceilings, non-owning object erasure, and stale generation rejection.
46. The full incremental exact-LLVM Debug tree rebuilt 75 affected steps
    warning-clean with eight workers. Runtime, application, LLVM,
    diagnostic-catalog, source-line-budget, and UVM source harness gates pass
    6/6 in 29.76 seconds; the application gate passes in 25.45 seconds.
    `git diff --check` is clean and `application_simulation.cpp` remains below
    policy at 2,484 lines. Final UVM 1.2 analysis under a 3-GiB address-space
    ceiling exits zero in 59.36 seconds at 1,688,596 KiB maximum RSS; final UVM
    2020-3.1 analysis exits zero under the same ceiling in 60.98 seconds at
    1,935,768 KiB maximum RSS. Retain
    `/tmp/fsim-uvm12-change9-final.log`,
    `/tmp/fsim-uvm12-change9-final-time.txt`,
    `/tmp/fsim-uvm2020-change9-final.log`, and
    `/tmp/fsim-uvm2020-change9-final-time.txt` as exact local evidence.
    Preserve accumulated Changes 1-9 and begin Change 10 with typed
    `uvm_config_db` set/get/exists/wait-modified behavior, hierarchical
    precedence, bounded wildcard/regular-expression matching, build/runtime
    precedence, and deterministic callback wakeup ordering. Do not reset,
    commit, push, run a sanitizer, or inspect hosted CI before Change 20.
47. Batch 159 Change 10 is complete in the same accumulated dirty worktree. A
    bounded simulation-owned config database reuses Change 9 resource records
    by exact setter-context, instance-pattern, field-pattern, and nominal-type
    key. Build-phase writes assign default precedence minus checked context
    depth; runtime writes restore default precedence. Matching values then use
    update order for same-precedence last-setting-wins behavior, while repeated
    exact sets update the existing resource rather than growing the entry map.
48. Instance and field glob patterns use bounded linear `*`/`?` matching. Raw
    `/.../` expressions compile to a restricted literal/any/escaped atom stream
    with `*`, `+`, and `?` quantifiers and execute through a recursion-free,
    explicitly work-capped dynamic-programming matrix. Unsupported grouping,
    alternation, classes, repetition blocks, malformed quantifiers/escapes, and
    excessive pattern work reject before entry/resource publication. Context,
    pattern, depth, entry, waiter, wake-fanout, and underlying resource budgets
    are all explicit.
49. One-shot wait-modified subscriptions resolve context-relative targets,
    prevalidate total wake fanout, detach matching waiters in registration order
    after the value and precedence are fully published, and contain callback
    exceptions without blocking later callbacks. Public `Simulation` ownership
    exposes the config database beside its resource pool. Application evidence
    proves build/runtime precedence, typed get/exists, and two-waiter order in
    interpreter, compiled, and debug engines. Runtime evidence additionally
    covers regular-expression instance/field matches, resource reuse, spelling,
    nominal mismatch, cancellation, callback failure, malformed-regex rejection,
    and entry/waiter/wake transaction ceilings.
50. The full incremental exact-LLVM Debug tree rebuilt 144 affected steps
    warning-clean with eight workers. Runtime, application, LLVM,
    diagnostic-catalog, source-line-budget, and UVM source harness gates pass
    6/6 in 27.84 seconds; the application gate passes in 24.62 seconds.
    `git diff --check` is clean and `application_simulation.cpp` remains below
    policy at 2,493 lines. Final UVM 1.2 analysis under a 3-GiB address-space
    ceiling exits zero in 47.79 seconds at 1,688,852 KiB maximum RSS; final UVM
    2020-3.1 analysis exits zero under the same ceiling in 76.70 seconds at
    1,895,244 KiB maximum RSS. Retain
    `/tmp/fsim-uvm12-change10-final.log`,
    `/tmp/fsim-uvm12-change10-final-time.txt`,
    `/tmp/fsim-uvm2020-change10-final.log`, and
    `/tmp/fsim-uvm2020-change10-final-time.txt` as exact local evidence.
    Preserve accumulated Changes 1-10 and begin Change 11 with command-line
    plusargs for factory, configuration, resource, verbosity, and timeout
    settings, including exact parsing, precedence, repeated-option ordering, and
    cataloged malformed-option diagnostics. Do not reset, commit, push, run a
    sanitizer, or inspect hosted CI before Change 20.
51. Batch 159 Change 11 is complete in the same accumulated dirty worktree. A
    bounded simulation-owned `SystemVerilogUvmCommandLineService` retains all
    ordered plusargs and unknown user options for later HDL consumption while
    recognizing the exact UVM 1.2/2020 factory aliases, integer/bitstream/string
    config forms, resource/config trace flags, global/component verbosity, and
    timeout spellings. Argument count, per-argument/total bytes, setting count,
    type/component/field/id/phase sizes, numeric width, and config/resource
    publication all have explicit rejection bounds. All recognized options
    parse before any factory, config, or resource mutation.
52. Application follows the upstream phase order: instance overrides precede
    source-ordered type overrides; config integers precede bitstreams and then
    strings; same-key config settings remain last-wins through the Change 10
    resource-backed database. The first repeated `+UVM_VERBOSITY` and
    `+UVM_TIMEOUT` values win while occurrence counts and component verbosity
    source order remain visible. Config integers use signed 32-bit semantics,
    the 2020 bitstream form permits bounded four-state binary/octal/hex or
    arbitrary decimal conversion into exactly 4096 bits, and UVM-style digit
    separators are accepted without opening an unbounded conversion path.
53. `Invocation` captures plusargs in order for run, debug, and standalone
    simulate. Each `Simulation` owns the command-line service beside its
    factory, resource pool, and config database; run/simulate/debug apply it
    before execution. Unknown plusargs remain retained, and malformed, bounded,
    or semantically inapplicable recognized settings emit cataloged
    `FSIM-UVM-CLI-001` diagnostics. `+UVM_TIMEOUT` intentionally remains UVM
    framework state and does not replace the simulator's independent
    `--duration`. Runtime evidence covers aliases, UVM phase ordering, repeated
    globals/config, wide and underscored values, trace flags, unknown flags,
    missing delimiters, late malformed transactionality, and byte ceilings.
    Application evidence covers factory/config/resource publication and state
    through interpreter, compiled, and debug engines plus positive and
    malformed real CLI paths.
54. The full exact-LLVM Debug tree rebuilt 76 affected steps warning-clean with
    eight workers. Runtime, application, LLVM, diagnostic-catalog,
    source-line-budget, and UVM source harness gates pass 6/6 in 28.31 seconds;
    the application gate passes in 25.08 seconds. `git diff --check` is clean,
    `application_simulation.cpp` is 2,499 lines, and graph review finds only
    bounded numeric-conversion loops under the fixed 4096-bit ceiling. Final
    UVM 1.2 analysis under a 3-GiB address-space ceiling exits zero in 54.97
    seconds at 1,688,680 KiB maximum RSS; final UVM 2020-3.1 analysis exits zero
    in 67.79 seconds at 1,935,524 KiB. Retain
    `/tmp/fsim-uvm12-change11-final.log`,
    `/tmp/fsim-uvm12-change11-final-time.txt`,
    `/tmp/fsim-uvm2020-change11-final.log`, and
    `/tmp/fsim-uvm2020-change11-final-time.txt` as exact local evidence.
    Preserve accumulated Changes 1-11 and begin Change 12 by defining and
    proving multi-root/multiple-context UVM singleton, factory, resource,
    configuration, callback, and command-line isolation or sharing rules. Do
    not reset, commit, push, run a sanitizer, or inspect hosted CI before
    Change 20.
55. Change 12 defines the ownership boundary in the public
    `uvm_context.hpp` compile-time contract. One independently constructed
    `Simulation` owns exactly one type registry, factory, resource pool,
    configuration database, callback domain, and command-line service; these
    are deliberately shared by all UVM roots in that simulation. Component
    paths alone are root-scoped, so equal `api_top` paths are valid and resolve
    independently under explicit root handles. Destroying the simulation owns
    teardown of the complete UVM context; no process-global UVM singleton state
    is retained.
56. Application evidence covers both sides of the contract. Two roots in one
    simulation share factory resolution, resource publication plus ordered
    callbacks/audits from both root identities, config values and waiter order,
    and parsed plusargs while retaining isolated equal component paths. A
    concurrently alive peer accepts the same root identity and registered type
    names, then publishes different factory overrides, resource values,
    callbacks, config values, and timeout/config plusargs without changing the
    original. After peer destruction, a newly constructed third simulation has
    four design-derived registry wrappers but empty mutable factory, resource,
    config, waiter, callback, command-line, and root state; its first root
    handle is 1, and the still-live original remains unchanged.
57. The full exact-LLVM Debug tree rebuilt 68 affected steps warning-clean with
    eight workers. Runtime, application, LLVM, diagnostic-catalog,
    source-line-budget, and UVM source harness gates pass 6/6 in 25.96 seconds;
    the application gate passes in 25.95 seconds. `git diff --check` is clean,
    `uvm_context.hpp` is 41 lines, `application_test_classes.cpp` is 1,609
    lines, and `application_simulation.cpp` remains 2,499 lines. Fresh graph
    review finds the public ownership contract, no matching process-global UVM
    state, no new production loops, and root/service ownership represented by
    the existing `Simulation::Impl` members. Final UVM 1.2 analysis under a
    3-GiB address-space ceiling exits zero in 55.32 seconds at 1,684,976 KiB
    maximum RSS; final UVM 2020-3.1 analysis exits zero in 69.60 seconds at
    1,932,436 KiB. Retain `/tmp/fsim-uvm12-change12-final.log`,
    `/tmp/fsim-uvm12-change12-final-time.txt`,
    `/tmp/fsim-uvm2020-change12-final.log`, and
    `/tmp/fsim-uvm2020-change12-final-time.txt` as exact local evidence.
    Preserve accumulated Changes 1-12 and begin Change 13 with report-message
    construction and `uvm_report_object` routing. Do not reset, commit, push,
    run a sanitizer, or inspect hosted CI before Change 20.
58. Batch 159 Change 13 is complete in the same accumulated dirty worktree. A
    simulation-owned `SystemVerilogUvmReportService` constructs stable messages
    with monotonic sequence numbers, generation-safe report-object handles and
    full names, severity, ID, payload, verbosity, file, line, and context.
    INFO reports use the simulation threshold unless already checked;
    warning/error/fatal reports route without repeating that INFO-only gate.
    UVM command-line global verbosity initializes the service, while default
    construction uses UVM_MEDIUM for INFO and UVM_NONE for other severities.
59. The bounded insertion-ordered element container retains packed integer,
    escaped string, and object elements plus per-element actions. Canonical
    composition emits log/display elements deterministically, preserves
    record-only elements without displaying them, validates live object
    generations, and contains route-hook exceptions. Focused evidence covers
    copy/erase/clear accounting, invalid kinds/actions/radices/severities,
    filtered and prechecked routing, stale report and element handles, exact
    count/name/string/packed/storage/field/composed-payload limits, and the
    explicit global handle. Application evidence routes through components in
    two roots with equal names but distinct handles, proves source/context and
    element composition, then proves concurrent peer thresholds, counters,
    sequences, and hooks diverge and a restarted simulation is clean.
60. The full exact-LLVM Debug tree rebuilt 75 affected steps warning-clean with
    eight workers. Runtime, application, LLVM, diagnostic-catalog,
    source-line-budget, and UVM source harness gates pass 6/6 in 29.12 seconds;
    the application gate passes in 25.79 seconds. `git diff --check` is clean,
    `uvm_report.hpp` is 248 lines, `uvm_report.cpp` is 468 lines,
    `runtime_uvm_report_tests.cpp` is 258 lines,
    `application_test_classes.cpp` is 1,695 lines, and
    `application_simulation.cpp` remains 2,499 lines. Fresh graph review finds
    only single-depth bounded element/string loops, no linear scan inside a
    loop, no recursion, and no process-global UVM report state. Final UVM 1.2
    analysis under a 3-GiB address-space ceiling exits zero in 55.62 seconds at
    1,688,556 KiB maximum RSS; final UVM 2020-3.1 analysis exits zero in 69.47
    seconds at 1,935,660 KiB. Retain
    `/tmp/fsim-uvm12-change13-final.log`,
    `/tmp/fsim-uvm12-change13-final-time.txt`,
    `/tmp/fsim-uvm2020-change13-final.log`, and
    `/tmp/fsim-uvm2020-change13-final-time.txt` as exact local evidence.
    Preserve accumulated Changes 1-13 and begin Change 14 with report-handler
    severity/ID actions, verbosity, files, hooks, overrides, default-file
    behavior, and hierarchical component policy. Do not reset, commit, push,
    run a sanitizer, or inspect hosted CI before Change 20.
61. Batch 159 Change 14 is complete in the same accumulated dirty worktree.
    Simulation-owned handler tables implement exact governed UVM precedence:
    `(severity,ID)` overrides ID, which overrides resolved severity/default for
    actions and nonzero file handles; `(severity,ID)` overrides ID, which
    overrides maximum verbosity. INFO/WARNING default to DISPLAY, ERROR to
    DISPLAY|COUNT, and FATAL to DISPLAY|EXIT. ID-specific severity overrides
    precede generic overrides and, exactly like UVM, an existing ID override
    table suppresses generic fallback even when that table has no entry for the
    current severity. Action and file policy is selected after the override.
62. Generic and resolved-severity hooks execute in that order for CALL_HOOK;
    both execute after one rejects, hook exceptions are contained/counted, and
    rejected messages do not consume routed sequence identity. Handler count,
    aggregate setting count, IDs, actions, and object generations are bounded.
    Every governed component `_hier` verbosity/action/file setter traverses the
    selected subtree in deterministic preorder without recursion. It snapshots
    only affected handler states and atomically rolls back the complete subtree
    if any descendant exceeds a cap. Runtime evidence covers all precedence,
    zero-file fallback, override, hook, exception, replacement, cap, invalid,
    hierarchy, sibling-isolation, and rollback paths. Application evidence
    updates the three-node `api_top` subtree while the equal `api_top` in a
    second root retains the simulation default.
63. The full exact-LLVM Debug tree rebuilt 76 affected steps warning-clean with
    eight workers. Runtime, application, LLVM, diagnostic-catalog,
    source-line-budget, and UVM source harness gates pass 6/6 in 29.67 seconds;
    the application gate passes in 26.20 seconds. `git diff --check` is clean,
    `uvm_report.hpp` is 432 lines, `uvm_report.cpp` is 499 lines,
    `uvm_report_handler.cpp` is 479 lines,
    `runtime_uvm_report_tests.cpp` is 515 lines,
    `application_test_classes.cpp` is 1,731 lines, and
    `application_simulation.cpp` remains 2,499 lines. Fresh graph review finds
    no recursion or process-global report state. Policy and hook resolution
    have no loops; bounded transactional hierarchy traversal has depth two and
    the graph's two `find`-inside-loop flags are ordered-map lookups, not linear
    scans. Final UVM 1.2 analysis under a 3-GiB address-space ceiling exits zero
    in 54.63 seconds at 1,688,668 KiB maximum RSS; final UVM 2020-3.1 analysis
    exits zero in 69.20 seconds at 1,935,416 KiB. Retain
    `/tmp/fsim-uvm12-change14-final.log`,
    `/tmp/fsim-uvm12-change14-final-time.txt`,
    `/tmp/fsim-uvm2020-change14-final.log`, and
    `/tmp/fsim-uvm2020-change14-final-time.txt` as exact local evidence.
    Preserve accumulated Changes 1-14 and begin Change 15 with report-server
    final formatting, severity/ID/quit accounting, max-quit behavior,
    file/display/log actions, and deterministic newline/numeric formatting. Do
    not reset, commit, push, run a sanitizer, or inspect hosted CI before
    Change 20.
64. Batch 159 Change 15 is complete in the same accumulated dirty worktree. A
    bounded simulation-owned `SystemVerilogUvmReportServer` composes exact UVM
    severity, optional verbosity, source, timestamp, object/context, ID,
    payload, and optional terminator fields. It accounts severity and ID before
    executing RECORD, DISPLAY, LOG, COUNT, EXIT, and STOP in governed order;
    masks standard output from MCD log destinations; contains sink exceptions;
    and exposes fixed-width deterministic summaries. Max-quit overridability,
    record-all, ID-summary, verbosity display, terminator display, count reset,
    and explicit count restoration are public. Severity, ID, quit, ID-table,
    and composed-output limits reject transactionally.
65. Report elements now format binary, octal, signed or unsigned decimal, and
    hexadecimal packed values with exact UVM prefixes. Decimal conversion uses
    bounded base-1e9 limbs over the existing 4,096-bit width ceiling; grouped
    octal and hexadecimal digits retain X/Z information. Runtime evidence
    covers exact composed text and newlines, every sink/action, MCD masking,
    stdout de-duplication, record-all, max-quit EXIT, STOP, summary/count state,
    sink failure, four-state numeric formats, and all resource bounds.
    Application evidence covers DISPLAY|LOG|RECORD|COUNT through a component
    handler, exact timestamp/source/context text, file mask 3-to-2, count-added
    EXIT, routed action visibility, and a clean restarted server.
66. The complete exact-LLVM Debug tree is warning-clean with eight workers.
    Runtime, application, LLVM, diagnostic-catalog, source-line-budget, and UVM
    source harness gates pass 6/6 in 38.81 seconds; the application gate passes
    in 35.60 seconds. `git diff --check` is clean. `uvm_report.hpp` is 549
    lines, `uvm_report.cpp` is 597 lines, `uvm_report_handler.cpp` is 479 lines,
    `uvm_report_server.cpp` is 298 lines, `runtime_uvm_report_tests.cpp` is 717
    lines, `application_test_classes.cpp` is 1,802 lines, and
    `application_simulation.cpp` remains 2,499 lines. Fresh graph review finds
    no recursion or process-global UVM report state; compose/process have no
    loops, summary traversal is single-depth, and numeric formatting is bounded
    to loop depth two with no linear scan inside a loop. Final UVM 1.2 analysis
    under a 3-GiB address-space ceiling exits zero in 55.71 seconds at
    1,688,456 KiB maximum RSS; final UVM 2020-3.1 analysis exits zero in 69.30
    seconds at 1,935,700 KiB. Retain
    `/tmp/fsim-uvm12-change15-final.log`,
    `/tmp/fsim-uvm12-change15-final-time.txt`,
    `/tmp/fsim-uvm2020-change15-final.log`, and
    `/tmp/fsim-uvm2020-change15-final-time.txt` as exact local evidence.
    Preserve accumulated Changes 1-15 and begin Change 16 with report catchers,
    ordered throw/catch/demote/modify behavior, re-entry and exception
    containment, recursion/resource bounds, removal during dispatch, and
    post-catcher accounting. Do not reset, commit, push, run a sanitizer, or
    inspect hosted CI before Change 20.
67. Batch 159 Change 16 is complete in the same accumulated dirty worktree.
    Each simulation now owns one bounded ordered report-catcher registry with
    global or exact report-object association, append/prepend order, opaque
    monotonic handles, callback enablement, and deterministic removal. A
    constrained mutable context exposes the governed severity, verbosity, ID,
    message, action, context, and element changes. Later callbacks see every
    retained earlier mutation. THROW continues; CAUGHT stops later callbacks
    and suppresses server accounting and routed sequence consumption. A
    severity change remaps the previous default action to the new severity's
    default unless that catcher explicitly set the action.
68. Dispatch snapshots bound work and exclude callbacks registered during the
    active traversal. Disablement or removal is rechecked before every callback,
    so one catcher can remove a later catcher without invalidating iteration.
    Callback exceptions, invalid results, or invalid/resource-excessive message
    mutations are contained and restore the exact pre-callback message before
    dispatch continues. Reports emitted from inside a catcher bypass recursive
    catcher dispatch, route normally, and reserve sequence identity before the
    outer report. Caught, demoted, invocation, failure, and re-entry counts plus
    deterministic summaries are simulation-local. The report server observes
    only the final thrown message. Runtime evidence covers global/instance and
    prepend/append order, chained modifications, automatic and explicit action
    behavior, caught suppression, exception rollback, removal, disablement,
    stale handles, re-entry, exact summaries, and registration/name/dispatch
    ceilings. Three-engine application evidence proves global-before-instance
    mutation/catch behavior, equal-root isolation, and clean peer/restart state.
69. The full exact-LLVM Debug tree rebuilt 75 affected steps warning-clean with
    eight workers. Runtime, application, LLVM, diagnostic-catalog,
    source-line-budget, and UVM source harness gates pass 6/6 in 33.12 seconds;
    the application gate passes in 27.54 seconds. `git diff --check` is clean.
    `uvm_report.hpp` is 652 lines, `uvm_report.cpp` is 600 lines,
    `uvm_report_handler.cpp` is 479 lines, `uvm_report_server.cpp` is 298 lines,
    `uvm_report_catcher.cpp` is 211 lines,
    `runtime_uvm_report_tests.cpp` is 975 lines,
    `application_test_classes.cpp` is 1,856 lines, and
    `application_simulation.cpp` remains 2,499 lines. Fresh graph review finds
    no recursion or process-global catcher state. Catcher registration has no
    loops; dispatch has two single-depth bounded traversals, and the graph's
    two find-inside-loop flags are ordered-map lookups rather than linear scans.
    Final UVM 1.2 analysis under a 3-GiB address-space ceiling exits zero in
    56.05 seconds at 1,688,516 KiB maximum RSS; final UVM 2020-3.1 analysis
    exits zero in 70.15 seconds at 1,935,668 KiB. Retain
    `/tmp/fsim-uvm12-change16-final.log`,
    `/tmp/fsim-uvm12-change16-final-time.txt`,
    `/tmp/fsim-uvm2020-change16-final.log`, and
    `/tmp/fsim-uvm2020-change16-final-time.txt` as exact local evidence.
70. Batch 159 Change 17 is complete in the same accumulated dirty worktree.
    The reported greater-than-20-GiB `fsim-sv` growth was reproduced under
    explicit address-space ceilings and traced past parsing into class method
    hydration. Each call site recursively copied every reachable UVM method
    body, materializing the call graph as a tree. Hydration now begins only at
    executable design-unit roots and expands each canonical method at most once
    per root; recursion and repeated calls retain profile-only references.
    Final unmodified analysis remains below the earlier 3-GiB cap at about
    1.69 GiB for UVM 1.2 and 1.94 GiB for UVM 2020-3.1.
71. Unmodified UVM 1.2 and UVM 2020-3.1 each compile with the same external
    object/registry/factory/config/report example and no library edits. UVM 1.2
    publishes its portable object in 121.33 seconds at 2,989,608 KiB maximum
    RSS under a 4-GiB ceiling. UVM 2020-3.1 publishes in 159.60 seconds at
    3,448,128 KiB under a 5-GiB ceiling. Portable reload repairs omit already-
    attached out-of-block method bodies, preserve process/chandle/null nominal
    types under repeated resolution, retain ordinary suspending class-task
    frames, and resolve legal self-qualified package classes, nested typedefs,
    constants, functions, and tasks without weakening genuine package cycles.
72. Each portable object combines with an independent peer object and
    elaborates two distinct aliased roots at both O0 and O2. UVM 1.2 O0/O2
    publications peak at 3,214,264/3,214,124 KiB under 4 GiB; UVM 2020-3.1
    peaks at 3,509,684/3,509,656 KiB under 5 GiB. Interpreter, compiled O0/O2,
    debug, application callback, cold/warm cache, VCD, and FST paths all emit
    two exact `FSIM-UVM-EXAMPLE-PASS payload=7 configured=11` lines. Both
    707-byte traces contain the `primary` and `secondary.nested` roots and
    exact transitions to payload 7, configured value 11, and pass 1.
73. The complete exact-LLVM Debug incremental build is warning-clean with eight
    workers. Frontend, elaboration, LLVM, application, runtime, diagnostic-
    catalog, source-line-budget, and UVM-source-harness gates pass. The two new
    class copyout diagnostics are cataloged. The simulation implementation is
    split into a 2,428-line core and 241-line accessor fragment while retaining
    application behavior. Preserve accumulated Changes 1-17 and begin Change
    18 with malformed/type/lifetime/override/regex/callback/resource negatives,
    relocation, restart, and cross-version invalidation evidence. Do not reset,
    commit, push, run a sanitizer, or inspect hosted CI before Change 20.
74. Batch 159 Change 18 is complete in the same accumulated dirty worktree.
    The object/component/registry/factory/resource/config/report runtime suites
    collectively reject malformed names, profiles and values; nominal type
    mismatches; stale, released and cross-owner handles; duplicate and cyclic
    overrides; invalid wildcard patterns; callback mutation, exception and
    re-entry failures; and every governed depth/count/name/path/report/resource
    ceiling transactionally. Failed work leaves no partial use count, callback,
    heap, hierarchy, resource, route, or accounting state.
75. The actual UVM 1.2 and UVM 2020-3.1 two-root O2 `.fsimdesign` directories
    copy to independent locations and execute compiled with the same exact two
    PASS lines. The application artifact gate separately proves portable object
    and design reload, mapped-library relocation, cold/warm cache identity,
    interpreter/compiled parity, repeated simulations in one process, and clean
    service state between restart peers. Future, truncated, trailing, malformed
    enumeration and incompatible object/design/class/HIR schemas reject before
    execution.
76. `fsim.library.artifact`, `fsim.artifact.object`,
    `fsim.artifact.design`, `fsim.application`, and `fsim.runtime` pass 5/5 in
    26.01 seconds. Preserve accumulated Changes 1-18 and begin Change 19 by
    synchronizing public UVM/version documentation, examples, diagnostics,
    evidence, provenance, inventories, audits, and performance/resource
    baselines. Do not reset, commit, push, run a sanitizer, or inspect hosted CI
    before Change 20.
77. Batch 159 Change 19 is complete in the same accumulated dirty worktree.
    README, architecture, language support, diagnostics, the new public UVM
    guide, governed source provenance, feature evidence, inventory, release
    audit, and candidate corpus now state the exact bounded Batch 159 contract.
    `SV-791` through `SV-800` advance the matrix to 1,240 executable rows and
    4,960 evidence cells across 518 paths: 226 test, 272 production, and 20
    release paths with 134 runtime owners. Matrix digest
    `49c0e3c30b0e0a1c1ebbd0c222acbc6551812e6c932512f0691b7ba49c2eda3b`
    and evidence digest
    `9151f73bfa2d952155d9fcc6325f865cc2ace16acc94084c58306b8a92d69099`
    are frozen into the release contracts.
78. The synchronized inventories cover 1,990 production diagnostics, 734
    bounded C/C++ sources, 832 SPDX-owned authored files, and 272 authored
    test/control files. The two governed upstream UVM trees remain external
    build products and are identified by exact archive, entry-point, and
    extracted-tree digests. Their final package/direct/portable/two-root
    memory measurements are recorded in `uvm-source-provenance.md`.
79. The complete documentation, conformance, release, inventory, installed-
    public, and Linux/Windows portability contract prefix passes 27/27 in
    17.48 seconds; `git diff --check` is clean. Preserve Changes 1-19 and begin
    Change 20 with full non-sanitized exact-LLVM Debug and Release eight-worker
    builds and 115-test regressions, then one commit and push. Do not run a
    sanitizer or inspect hosted CI because Batch 159 is not a monitoring
    boundary.
80. Batch 159 Change 20 qualification initially passed 114/115 Debug tests and
    exposed one integration regression: the new parser-neutral
    `@sv-select:<member>` representation remained attached to non-class struct/
    union member selections after an unpacked index. The graph trace reached
    `Resolver::resolve_call` and the `FSIM-ELAB-043` fallback. Class resolution
    now folds a non-class receiver back to the legacy indexed aggregate text
    while preserving the neutral representation long enough to resolve
    class-valued indexed members. A focused frontend regression, the full UVM
    application, and `fsim.application.sv_aggregate_multidimensional` pass
    together.
81. Change 20 and Batch 159 are complete. The final exact LLVM 22.1.8 Debug
    tree rebuilds warning-clean with eight workers and passes 115/115 tests in
    414.95 seconds. The Release tree completes a fresh 494-step eight-worker
    warning-clean build and passes 115/115 in 336.28 seconds. Both full runs
    include the 27 documentation/conformance/release/inventory/installation/
    portability contracts, UVM source harness, artifact matrices, frontend,
    elaboration, LLVM, application, runtime, and the repaired aggregate gate.
82. Batch 159 is not a ten-batch monitoring boundary, so no sanitizer ran and
    hosted CI was not inspected. Close out with one accumulated commit and push
    containing Changes 1-20 and this handoff. Before Batch 160 implementation,
    save and push its restart plan, then clear context as required by the new
    batch flow.

## Batch 158 completed checkpoint - 2026-08-07

1. Start in `/home/colin/projects/fsim`, read this file and the authoritative
   expanded Batch 158 allocation in `implementation_plan_v2.md`, and verify
   branch `codex/v2` remains based on pushed documentation checkpoint
   `dd9d2332411646e12797e4329bef20168bb64504`. Preserve the dirty accumulated
   Batch 158 worktree described below; do not reset, commit, push, inspect
   hosted CI, or run a sanitizer before Change 20.
2. Change 1 is complete in the dirty worktree. The distinct public VHPI C ABI
   fixes explicit host and plug-in versions and structure sizes, native pointer
   width, reserved flags, stable nonpointer 64-bit handles, simulation
   ownership, bounded diagnostic views, Windows/POSIX calling and export
   conventions, and the exact `fsim_vhpi_plugin_bind_v1` symbol.
3. The C++ constructor and validators reject incompatible host and plug-in
   versions, truncated tables, foreign pointer widths, reserved flags, zero
   simulation ownership, missing report state, malformed bounded plug-in names,
   and incomplete lifecycle tables. An independent C translation unit freezes
   the 40-byte host and 48-byte plug-in layouts and exercises the diagnostic
   callback and bind-symbol spelling through the public C header.
4. The affected exact-LLVM 22.1.8 Debug runtime target rebuilt warning-clean
   with eight workers. `fsim.runtime` and `fsim.source-line-budget` pass 2/2
   in 0.21 seconds, tracked and untracked whitespace checks are clean, and no
   sanitizer or hosted CI was run.
5. Preserve this checkpoint and begin Change 2 by loading one VHPI image through
   `platform::DynamicLibrary`, resolving only the exact bind symbol,
   validating and copying the descriptor transactionally, containing bind,
   startup, and shutdown failures, and guaranteeing exactly-once teardown
   before unload.
6. Change 2 is complete in the same dirty worktree. Loading validates the host
   before opening an image, resolves only the exact bind symbol, invokes bind
   and startup behind exception boundaries, copies the validated descriptor
   before publication, and keeps the image owned until shutdown completes.
   Failed startup never invokes shutdown; successful explicit, automatic, and
   move-owned teardown invoke it exactly once and retain the first result.
7. A real hidden-visibility image covers successful publication, explicit and
   automatic shutdown, status failures and exceptions at bind/startup/shutdown,
   malformed versions/sizes/flags/name/lifecycle, missing bind symbol, invalid
   host ownership, and missing artifacts. The exact-LLVM Debug target builds
   warning-clean with eight workers; `fsim.runtime` and
   `fsim.source-line-budget` pass 2/2 in 0.21 seconds, and whitespace checks
   are clean.
8. Preserve this checkpoint and begin Change 3 with simulation-owned error
   state and separate generation-qualified object and iterator handle
   registries. Distinguish malformed, stale, released, exhausted, and
   cross-simulation identities deterministically; never reuse VPI identities or
   expose host addresses.
9. Change 3 is complete in the same dirty worktree. Mutex-safe error state owns
   bounded code/message storage, validates raw severities before narrowing,
   remains inspectable until the next call boundary, and preserves the last
   valid record when malformed input is rejected.
10. VHPI object and iterator handles use a VHPI-only interface tag, independent
    kind tag, process-unique registry identity, 16-bit generation, and 24-bit
    slot; they cannot alias VPI identities or host addresses. Object records
    retain kind, parent, stable creation order, and live-child count. Iterators
    own immutable handle snapshots and release independently.
11. Focused evidence covers two simulations, malformed and cross-owner
    identities, unknown kinds, leaf-first release, object/iterator confusion,
    ordered scan and exhaustion, independent iterator teardown, released
    identities before reuse, stale identities after reuse, and owned diagnostic
    lifetime and limits. The exact-LLVM Debug target builds warning-clean with
    eight workers; `fsim.runtime` and `fsim.source-line-budget` pass 2/2 in
    0.20 seconds, and whitespace checks are clean.
12. Preserve this checkpoint and begin Change 4 with canonical case-insensitive
    basic identifiers, exact extended identifiers, selected/indexed full-name
    identity, hierarchy-region classification, source metadata, checked
    root/relative lookup, and creation-ordered relationship iterators.
13. Change 4 is complete in the same dirty worktree. Named publication owns
    canonical case-folded basic identifiers, case-exact extended identifiers,
    signed multidimensional index identity, selected and full names, and
    bounded file/line/column source records. Duplicate sibling/full identities,
    malformed identifiers/indices/sources, anonymous named parents, and
    malformed or missing lookups reject before publication.
14. Checked full and relative lookup preserves extended-name dots and case while
    normalizing basic-name case and indexed-name whitespace. Children, region,
    and declaration relationships snapshot live handles atomically and scan in
    stable creation order through independently releasable iterators.
15. Focused evidence covers named multi-root isolation, basic and extended
    identifiers, two-dimensional signed generate indices, indexed extended
    signals, source ownership, duplicate and malformed profiles, exact/missing
    lookup, relationship filtering/order, unknown relationships, cross-owner
    rejection, and leaf-first name removal. The first build stopped only on
    warnings-as-errors for an older anonymous aggregate missing explicit new
    metadata fields; after that correction the exact-LLVM Debug target builds
    warning-clean with eight workers, and `fsim.runtime` plus
    `fsim.source-line-budget` pass 2/2 in 0.21 seconds.
16. Preserve this checkpoint and begin Change 5 with canonical type/subtype
    identities; scalar categories, base-type links, recursive range and
    constraint descriptors, direction and resolution metadata, declaration
    queries, ownership checks, and strict depth/node/resource limits.
17. Change 5 is complete in the same dirty worktree. A simulation-owned type
    system publishes one immutable descriptor per canonical Type/Subtype object,
    retains exact scalar category, direct subtype link, canonical base identity,
    recursive constraints, inclusive direction/null-range semantics, and
    optional checked resolution-subprogram identity without native layout.
18. Recursive constraint validation bounds depth, total nodes, and children and
    rejects malformed shapes, unknown categories/directions, inconsistent
    ranges, invalid base chains, wrong resolution objects, duplicate
    publication, and cross-simulation identities before copying. Signal,
    variable, constant, and file declarations bind once to a published type and
    return owned descriptor snapshots.
19. Focused evidence covers integer, bit, resolved, two-level subtype, signed
    ascending/descending and explicit-null ranges, nested array/record
    constraints, immutable snapshots, canonical base queries, declaration
    queries, duplicates, unknown scalar kinds, missing bases, bad directions,
    wrong resolvers, recursive-depth limits, and two-simulation isolation. The
    exact-LLVM Debug target builds warning-clean with eight workers;
    `fsim.runtime` and `fsim.source-line-budget` pass 2/2 in 0.20 seconds,
    and whitespace checks are clean.
20. Preserve this checkpoint and begin Change 6 with checked scalar, enumeration,
    physical, access, and null value transfer tied to declaration/type identity,
    including exact positions, units, designated subtype metadata, caller
    buffer bounds, owned storage, and lossless read/write validation.
21. Change 6 is complete in the same dirty worktree. A simulation-owned value
    system binds one typed value profile to each declaration and stores only
    semantic payloads: booleans/bits, Unicode characters, signed integers,
    finite reals, full-width time, enumeration positions, physical magnitude
    plus unit position, and access object handles where zero is the exact null
    value. No native object address or layout enters the value model.
22. Enumeration literal tables, physical unit names/multipliers, and designated
    access subtype identity are copied and validated before publication.
    Integer constraints, Unicode scalar legality, finite-real policy, enum/unit
    positions, and exact access target types are checked transactionally before
    writes. Bounded literal reads report the required size and leave undersized
    caller buffers untouched.
23. Focused evidence covers boolean, Unicode character, constrained signed
    integer, real, maximum-width time, owned enumeration text, negative physical
    values and exact unit metadata, access null/non-null transfer, range/type/
    position/unit/designated-subtype failures, malformed profiles, duplicate
    binding, and rejected-write rollback. The first full compile required three
    test-only explicit result conversions; production compiled cleanly. The
    corrected exact-LLVM Debug target builds warning-clean with eight workers,
    and `fsim.runtime` plus `fsim.source-line-budget` pass 2/2 in 0.21
    seconds.
24. Preserve this checkpoint and begin Change 7 with constrained and
    unconstrained arrays plus records, declared multidimensional index mapping,
    recursive element/field values, exact shape/type identity, caller buffer
    sizing, and transactional composite publication and updates.
25. Change 7 is complete in the same dirty worktree. A simulation-owned
    composite system publishes immutable array and record descriptors leaf
    first, distinguishes constrained, runtime-constrained, and explicit-null
    shapes, owns ordered record fields, and recursively validates scalar and
    composite values against exact type identity with bounded depth and nodes.
26. Multidimensional array values retain signed declared ranges and direction;
    checked row-major translation supports ascending and descending dimensions
    without exposing native layout. Exact member reads report required size,
    leave undersized buffers untouched, and copy only after full validation.
    Whole-value and element updates validate a private copy before publication.
27. Focused evidence covers a signed two-dimensional matrix, all four declared
    index translations, descending runtime-constrained arrays, explicit-null
    arrays, nested matrix-in-record values, ordered fields, exact and short
    buffers, element updates, duplicate fields, wrong shapes/types/indices, and
    rejected-write rollback. The exact-LLVM Debug target builds warning-clean
    with eight workers; `fsim.runtime` and `fsim.source-line-budget` pass 2/2
    in 0.21 seconds, and tracked and untracked whitespace checks are clean.
28. Preserve this checkpoint and begin Change 8 with file, protected, resolved,
    and nine-state type/value semantics. Keep opaque file/protected identities
    separate from native resources, define resolution metadata and driver-facing
    value contracts explicitly, and cover all nine logic states losslessly.
29. Change 8 is complete in the same dirty worktree. File declarations open as
    simulation-owned opaque nonpointer identities with copied logical names,
    immutable type/access metadata, checked read/write/append permission, unique
    reopen identities, deterministic close state, and cross-simulation rejection.
    No native stream, descriptor, mutex, or protected-object address is exposed.
30. Protected variables use independently tagged shared/exclusive lease
    identities tied to a checked process or subprogram caller. Conflicting
    access returns busy without publication, wrong-caller and repeated release
    are deterministic, and foreign systems reject leases before lookup.
    Resolved types separately retain copied resolver provenance only when the
    semantic type's canonical resolution-subprogram identity agrees.
31. Exact nine-state scalar and vector values reuse the runtime's lossless
    four-plane representation. VHPI byte encoding preserves U, X, 0, 1, Z, W,
    L, H, and don't-care in declared text order; short buffers report the exact
    requirement without writes, and wrong-width updates retain prior state.
32. Focused evidence covers file permission, duplicate/open/close/reopen and
    foreign identities; shared/exclusive protected leases; copied/mismatched
    resolver provenance; all nine states, scalar and vector transfer, buffers,
    duplicate binding, rollback, cross-owner and released handles. The first
    production compile required two exact result-field/default-construction
    corrections, and the test compile required its local throwing assertion
    helper. The corrected exact-LLVM Debug target builds warning-clean with
    eight workers; fsim.runtime and fsim.source-line-budget pass 2/2 in
    0.21 seconds, source files remain below 2,000 lines, and tracked/untracked
    whitespace checks are clean.
33. Preserve this checkpoint and begin Change 9 with signal drivers, sources,
    projected transactions, waveform elements, rejection and inertial/transport
    policy, and deterministic relationship queries over the common scheduler.
    Keep every driver and transaction identity simulation-owned and do not add
    deposit/force/release semantics reserved for Change 10.
34. Change 9 is complete in the same dirty worktree. A VHPI driver facade uses
    the common scheduler's cancelable update tasks while retaining independently
    tagged opaque driver, source, and projected-transaction identities. Signal
    and source handles remain checked VHPI objects, driver/source relationships
    are creation ordered, and unresolved signals reject a second driver.
35. Resolved logic vectors apply the existing IEEE nine-state resolution table
    independently per element. Projected waveforms require nonempty, matching-
    width, strictly ascending elements without time overflow. Later assignments
    truncate pending transactions at or after their first time; inertial mode
    additionally applies the checked rejection window and equal-value bridge
    rule, while transport mode retains earlier transactions exactly.
36. Pending transaction snapshots preserve identity, driver/signal ownership,
    copied value, absolute time, rejection, delay mode, and waveform index in
    deterministic order. Scheduler callbacks commit only a still-pending
    identity, update its driver slot, erase that transaction, and recompute the
    resolved visible signal. Destruction cancels every retained task.
37. Focused evidence covers ordered drivers/sources, duplicate sources, width
    mismatch, unresolved-driver limits, cross-simulation identity, transport
    elements and replacement, commits at times 3 and 4, inertial pulse rejection
    and commit at time 7, empty/descending/wrong-width/rejection failures, and
    unchanged pending state after rejection. The first production build exposed
    the missing packed-value include, explicit packed defaults, and an invalid
    diagnostic-ID narrowing; the test build required two explicit result
    conversions. The corrected exact-LLVM Debug target builds warning-clean
    with eight workers; fsim.runtime and fsim.source-line-budget pass 2/2 in
    0.21 seconds, all new sources remain below 2,000 lines, and tracked/untracked
    whitespace checks are clean.
38. Preserve this checkpoint and begin Change 10 with immediate and delayed
    deposit, force, release, and transaction cancellation over checked signal
    types and owners. Define force layering and retained operation identities,
    guarantee rollback on invalid or partially prepared requests, and keep the
    scheduler's underlying driver activity visible again after release.
39. Change 10 is complete in the same dirty worktree. Deposits, forces, and
    releases retain independently tagged opaque operation identities with exact
    signal, process/subprogram owner, kind, target-force, copied value, absolute
    time, pending/active/canceled/completed state, and cancelable scheduler task.
40. Immediate and delayed deposits update the underlying signal value and are
    superseded by the next driver commit. Forces form ordered layers: the newest
    active layer masks visible value while projected driver activity continues
    updating underneath. Releasing any exact owned layer removes only that
    layer; releasing the top exposes the next force or latest underlying value.
41. Delayed writes validate all signal/type/width/owner/time/force state before
    publishing an operation. Owner-checked cancellation changes only a pending
    identity and leaves completed operations queryable. Projected transaction
    cancellation likewise requires the exact driver source and erases only the
    selected pending transaction. Invalid and foreign requests publish nothing.
42. Focused evidence covers immediate deposit and driver supersession, force
    masking through a driver commit, release recovery, two-layer force order,
    delayed force cancellation, delayed deposit and release, driver progress
    beneath a pending release, selected transaction cancellation, completed and
    canceled operation queries, wrong width/owner/force, rollback, and foreign
    operation identity. The complete exact-LLVM Debug target builds warning-
    clean with eight workers on its first matrix build; fsim.runtime and
    fsim.source-line-budget pass 2/2 in 0.21 seconds, all affected sources remain
    below 2,000 lines, and tracked/untracked whitespace checks are clean.
43. Preserve this checkpoint and begin Change 11 with exact simulation time,
    unit, precision, and delta queries plus phase callbacks for update,
    postponed/read-only, next-time, synchronization, save, restart, reset, and
    terminal regions. Reuse scheduler phase ordering where it matches, define
    the VHPI-only regions explicitly, and retain removable callback identities.
44. Change 11 is complete in the same dirty worktree. A simulation-owned time
    service reports exact ticks, delta count, unit and precision exponents,
    current scheduler phase, and next pending time. Profiles are bounded to
    decimal exponents -18 through 18 and reject precision coarser than the
    declared unit before installing any scheduler hook.
45. Update, synchronization, and read-only callbacks map respectively to the
    scheduler's update, reactive, and postponed safe points. Next-time
    callbacks announce each distinct future timestamp once. Save, restart,
    reset, and terminal remain explicit VHPI lifecycle notifications rather
    than invented scheduler phases. Callback snapshots dispatch in creation
    order outside the registry lock, one-shot callbacks deactivate before
    invocation, and retained identities remain queryable after removal.
46. Focused evidence covers initial and in-phase time queries, two simulation
    times, repeat and one-shot callback counts, deterministic next-time
    announcement, removal and repeated removal, all four explicit lifecycle
    regions, invalid profiles/phases, and cross-simulation callback rejection.
    The complete exact-LLVM Debug target builds warning-clean with eight
    workers; fsim.runtime and fsim.source-line-budget pass 2/2 in 0.21 seconds,
    the three new files contain 136, 251, and 213 lines, and tracked/untracked
    whitespace checks are clean.
47. Preserve this checkpoint and begin Change 12 with signal, process, event,
    transaction, assertion, and lifecycle callbacks. Copy event data before
    dispatch, permit safe self/peer removal and nested registration/re-entry,
    contain callback exceptions, and guarantee deterministic teardown.
48. Change 12 is complete in the same dirty worktree. A simulation-owned VHPI
    callback service retains independently tagged callback identities, exact
    category and optional object filters, user data, repeat/one-shot policy,
    creation ordinal, invocation count, and active/fired/removed/failed/
    torn-down state without exposing callback closures.
49. Signal, process, generic event, transaction, assertion, and all eight
    start/end simulation/save/restart/reset event families validate object kind
    and ownership before publication. Each event owns its nine-state value,
    message, source location, severity, correlation identity, scheduler time,
    delta, simulation identity, and callback user data before dispatch.
50. Dispatch snapshots active identities in creation order, rechecks each
    identity before invocation so peer removal is effective immediately,
    invokes outside the registry lock, defers nested registrations until the
    next matching publication, permits nested publication, contains every
    callback exception, and destroys retained closures outside the lock in
    creation order during idempotent teardown.
51. Focused evidence covers all five runtime event families, copied payloads,
    scheduler time, exact object/global filters, self and peer removal, nested
    registration and re-entry, exception containment with later-callback
    progress, every lifecycle kind, invalid kind/object/request profiles,
    cross-simulation identities and objects, and deterministic teardown. After
    one mechanical include/explicit-aggregate correction, the complete
    exact-LLVM Debug target builds warning-clean with eight workers;
    fsim.runtime and fsim.source-line-budget pass 2/2 in 0.21 seconds, the three
    new files contain 165, 374, and 406 lines, and whitespace checks are clean.
52. Preserve this checkpoint and begin Change 13 with foreign subprogram and
    foreign-model registration, copied/validated profiles, call contexts,
    argument/result handles, lifecycle, re-entry, unregister, retained user
    data, and deterministic containment without exposing C++ ownership.
53. Change 13 is complete in the same dirty worktree. A simulation-owned
    foreign registry copies and validates case-insensitive subprogram/model
    names, ordered unique parameter profiles, exact type and mode identities,
    optional result type, registration user data, and model start/invoke/stop
    callbacks before publishing an independently tagged registration identity.
54. Each invocation owns a distinct call identity, checked live scope, copied
    typed nine-state arguments, independently tagged argument/result handles,
    call user data, completion/failure state, and execution error. Invocation
    callbacks run outside the registry lock, so same-registration re-entry is
    isolated; result publication is active-call-only, type exact, single-shot,
    and required for result-bearing subprograms.
55. Models require explicit start and stop callbacks and cannot invoke before
    start, stop with an active call, or unregister while started. Subprograms
    reject model lifecycle callbacks. Active calls prevent unregister/release;
    completed-call release invalidates all child handles. Exceptions are
    contained, and unregister/teardown release retained closures outside the
    lock in registration order without exposing C++ ownership.
56. Focused evidence covers copied profiles, ordered argument handles, typed
    results, registration/call user data, nested same-registration invocation,
    active-call unregister rejection, model start/invoke/stop, callback
    exception and missing-result failures, invalid scope/arity, explicit call
    release, repeated unregister, teardown, and foreign registration/call/
    argument identities. The complete exact-LLVM Debug target builds warning-
    clean with eight workers; fsim.runtime and fsim.source-line-budget pass 2/2
    in 0.21 seconds after teardown hardening, the three new files contain 230,
    591, and 253 lines, and tracked/untracked whitespace checks are clean.
57. Preserve this checkpoint and begin Change 14 with generic and port
    association queries, exact formal/actual/mode/class metadata, open and
    disconnected associations, object and call user data, stable owner
    identity, and strict cross-root rejection.
58. Change 14 is complete in the same dirty worktree. A simulation-owned
    association facade transactionally publishes immutable generic and port
    descriptors with independently tagged identities, exact owner and root,
    formal/actual handles, mode, object class, actual kind, creation ordinal,
    and independently retained object and call user data.
59. Object actuals require a live class-matching declaration. Open and
    disconnected actuals use explicit kinds and a zero object identity;
    disconnected is port-only. Port profiles require signal class, while
    generic profiles preserve constant, signal, variable, file, and type-class
    identity. Duplicate formals and repeat owner publication reject without
    partial state.
60. Publication walks the owner, every formal, and every object actual to its
    canonical live root with a bounded ancestry traversal. A different root is
    rejected even when both roots belong to the same simulation, and foreign
    handles are distinguished before root comparison. Owner queries filter by
    generic/port kind while preserving global creation order.
61. Focused evidence covers a constant generic, connected inout port, open
    output, disconnected input, exact formal/actual/mode/class/root metadata,
    stable ordering, mutable object/call user data, repeat publication,
    invalid owner, duplicate formal, transactional same-simulation cross-root
    rejection, and foreign owner/association identity. The complete exact-LLVM
    Debug target builds warning-clean with eight workers on its first build;
    fsim.runtime and fsim.source-line-budget pass 2/2 in 0.21 seconds, the three
    new files contain 142, 295, and 229 lines, and whitespace checks are clean.
62. Preserve this checkpoint and begin Change 15 with assertion, report, and
    output services plus multiple-root/context isolation, copied severity and
    source metadata, bounded formatting, sink exception containment, and
    deterministic interleaving.
63. Change 15 is complete in the same dirty worktree. A simulation-owned VHPI
    I/O service publishes independently tagged named contexts bound to one
    exact live root. Multiple contexts may share a root, multiple roots remain
    isolated, and every context retains creation order, active state, event
    count, and its own copied history alongside one global history.
64. Assertion, report, and raw output events retain a single global ordinal,
    exact context/root/subject identity, severity, formatted message, and
    optional copied source. True assertions publish nothing. A bounded 64 KiB,
    64-argument formatter supports exact `{}` substitution plus escaped braces
    and rejects malformed or surplus fields before publication.
65. Root ancestry is checked before assertion/report publication; output has no
    fabricated subject or source. A recursive dispatch lock serializes every
    context while permitting sink-triggered nested output, so nested and
    multi-context events have deterministic ordinals. Events enter both
    histories before the sink runs; sink exceptions are contained without
    erasing evidence, and teardown releases sinks outside the registry lock in
    context order while retaining inactive descriptors and history.
66. Focused evidence covers two roots, two contexts on one root, exact report
    formatting and source metadata, true/false assertions, raw output, nested
    sink output, deterministic global ordinals, per-context/global histories,
    same-simulation cross-root and foreign-simulation rejection, invalid
    severity/format/source/text bounds, throwing-sink retention, and idempotent
    teardown. The complete exact-LLVM Debug target builds warning-clean with
    eight workers on its first build; fsim.runtime and
    fsim.source-line-budget pass 2/2 in 0.22 seconds, the three new files
    contain 163, 405, and 224 lines, and whitespace checks are clean.
67. Preserve this checkpoint and begin Change 16 with same-process restart and
    portable artifacts, exact schema/ABI/content/cache/plug-in provenance,
    canonical handle remapping, explicit native-state invalidation, relocation,
    and mapped-library identity.
68. Change 16 is complete in the same dirty worktree. A fixed-width VHPI
    checkpoint artifact owns its schema, host and plug-in ABI versions and
    structure sizes, pointer width, source simulation identity, design-content
    and native-cache fingerprints, mapped-library identities, plug-in
    provenance, canonical exported-object identities, and a native-state
    summary without retaining host addresses.
69. Capture resolves each caller-selected live object to its canonical full
    name and exact VHPI kind, rejects anonymous, duplicate, malformed, and
    cross-simulation identities before publication, and retains the source
    handle only as a logical remap key. Same-process restart requires the exact
    simulation, object handles, and native-owner summary. Portable restore
    resolves every object by canonical name and kind in the already-elaborated
    target registry, returns old-to-new handle mappings, and leaves the target
    untouched on any mismatch.
70. Compatibility checks distinguish schema, host ABI, plug-in ABI, pointer
    width, design content, cache, mapped-library content, and plug-in/host
    provenance mismatches. Relocated library and plug-in paths are accepted
    only when their stable identities, mapping, and content fingerprints still
    match; each changed path is reported explicitly.
71. Portable restore rejects pre-existing native owners and reports explicit
    invalidations for callback closures/user data, foreign subprograms/models/
    user data, active foreign calls, open files, protected leases, scheduled
    transactions, I/O sink contexts, and dynamic-library plug-in contexts.
    Focused evidence covers exact restart handles, cross-simulation and changed
    native state, every compatibility mismatch family, transactional missing
    and wrong-kind objects, canonical cross-registry remapping, all eleven
    invalidation classes, two relocation records, duplicate exports, and
    cross-simulation capture.
72. The complete exact-LLVM Debug target builds warning-clean with eight
    workers after one test-only standard-header correction;
    `fsim.runtime` and `fsim.source-line-budget` pass 2/2 in 0.21 seconds.
    The three new files contain 180, 385, and 259 lines, duplicate source
    registration was removed during the final audit, and tracked/untracked
    whitespace checks are clean. Preserve this checkpoint and begin Change 17
    with independently built C and C++ VHPI images that exercise the public ABI
    through host-like loading rather than direct C++ runtime calls.
73. Change 17 is complete in the same dirty worktree. The append-only public C
    ABI retains the exact 40-byte v1 reporting host and adds a 56-byte v2 host
    whose v1 prefix is followed only by a service context and callback. Fixed
    48-byte request and 40-byte result records carry operation, flags, bounded
    text, VHPI handle, value, and user-data fields across thirteen explicit
    hierarchy, type, value, driver, control, time, callback, foreign,
    association, I/O, user-data, checkpoint, and lifecycle service families.
74. Independently compiled C and C++ shared images include only the public
    `vhpi_abi.h` plus language-standard headers. Each requires a checked v2
    host, publishes the same v1 plug-in descriptor contract, reports startup,
    invokes every service family in deterministic order, and invokes shutdown
    through the lifecycle service. The hardened loader still accepts existing
    v1 lifecycle images; a service image deterministically rejects a v1-only
    host during bind.
75. The platform fixture validates C/C++ layout prefixes and sizes, service
    fields and returned handles, distinct image names/diagnostics/user data,
    exactly-once shutdown, no calls after unload, two complete loads of each
    original image, and byte-copied relocated loads with identical transcripts.
    Dynamic-symbol inspection shows each hidden-visibility image exports only
    `fsim_vhpi_plugin_bind_v1`.
76. The first focused test run exposed only the expected legacy negative-probe
    adjustment: host version 2 is now valid and requires the v2 size, so the
    unknown-version test now probes version 3. After that correction, the
    complete exact-LLVM Debug target builds warning-clean with eight workers;
    `fsim.runtime` and `fsim.source-line-budget` pass 2/2 in 0.21 seconds.
    The C image, C++ image, host fixture, and public ABI header contain 106,
    116, 272, and 146 lines, and whitespace checks are clean.
77. Preserve this checkpoint and begin Change 18 with consolidated negative
    matrices for malformed ABI/profile inputs, invalid/stale/released/
    cross-owner handles and iterators, short buffers, callback removal,
    recursive/reentrant and exception paths, resource bounds, transactional
    rollback, and post-unload behavior across the completed VHPI surface.
78. Change 18 is complete in the same dirty worktree. One consolidated
    cross-surface matrix complements the per-feature rejection tests with
    thirteen ordered cases spanning malformed and cross-owner handles,
    released and generation-stale object/iterator snapshots, enum buffer
    sizing, invalid-write rollback, malformed-profile nonpublication,
    recursive constraint limits, immediate callback peer removal, nested
    callback re-entry and exception containment, and callback object/owner
    validation.
79. The independent-image host fixture now also drives the raw v2 service
    boundary with null ownership/request, truncated request/result tables,
    unknown service families, missing bounded text, and a request after image
    unload. Every case returns its exact invalid-argument or stale-handle status
    without publishing a result or additional host transcript.
80. Together with the retained Changes 1-17 matrices, the focused runtime gate
    covers malformed host/plug-in ABI and descriptors, invalid/stale/released/
    cross-simulation object and iterator identities, short buffers, callback
    removal, recursive and reentrant callbacks, bind/startup/shutdown and
    callback exceptions, depth/node/text/count resource bounds, transactional
    publication/write/restore rollback, and post-unload rejection.
81. The complete exact-LLVM Debug target builds warning-clean with eight
    workers on its first Change 18 build; `fsim.runtime` and
    `fsim.source-line-budget` pass 2/2 in 0.22 seconds. The consolidated
    matrix has 260 lines, the expanded platform fixture has 316 lines, and
    tracked/untracked whitespace checks are clean. Preserve this checkpoint
    and begin Change 19 with real interpreter/LLVM O0/O2, mixed-language,
    multi-root, cold/warm cache, standalone object/design, mapped-library,
    relocation, and save/restart integration differentials, then synchronize
    the public VHPI documentation, examples, diagnostics, matrices,
    inventories, audits, and restart handoff.
82. Change 19 is complete in the same dirty worktree. The independent C and
    C++ reference images produce matching deterministic service transcripts
    under interpreter, compiled O0, and compiled O2 labels while retaining
    distinct simulation ownership. Repeated loads cover the warm path, and
    copied images retain exact startup/service/shutdown behavior after
    relocation. The owning application, API, mapped-library, object/design
    artifact, cache, and runtime integration gates pass 7/7 in 25.79 seconds.
83. Public README, architecture, language support, diagnostics, the new
    `vhdl-vhpi.md` guide with a buildable public-C example, test inventory, and
    executable feature rows `VH-270` through `VH-279` now describe and own the
    completed ABI, hierarchy/type/value/driver/callback/foreign/association/I/O,
    checkpoint, invalidation, integration, and negative surfaces.
84. The frozen candidate advances to 1,230 executable rows, 4,920 linked
    evidence cells, and 493 exact paths split 218 test, 259 production, and 16
    release paths. It owns 133 runtime files and 36 corpus CTests. The matrix
    SHA-256 is
    `0878effd1889b17cf7517a952c054a8fe470b0699fca760089b8520f7b4ae4c9`;
    the evidence-path SHA-256 is
    `b153df1b3eec1c2b6cd606db60f60554d668c2b02f111bb4725daf25b64daabb`.
85. Inventories freeze 1,989 diagnostics, 703 bounded C/C++ sources, 796
    SPDX-owned artifacts, and 265 authored test/control files. Differential
    evidence covers 486 interpreter, 403 LLVM, 267 cache, 109 debugger, 137
    VCD/trace, 454 scheduling, and 110 failure rows. The strict VHDL audit owns
    all 287 VHDL/release rows through 53 distinct runtime files.
86. The nine diagnostic-catalog, source-policy, legality, VHDL, differential,
    public, inventory, release-candidate, and installed-public-contract gates
    pass 9/9 in 14.27 seconds; `git diff --check` is clean. Preserve the
    accumulated dirty worktree and begin Change 20 with full non-sanitized
    exact-LLVM Debug and Release builds, full regressions, and release gates.
    Then commit and push exactly once. Do not run a sanitizer or inspect hosted
    CI because Batch 158 is not a monitoring boundary.
87. Change 20 is complete. The exact-LLVM 22.1.8 Debug tree relinks all 17
    affected executables warning-clean with eight workers and passes the full
    suite 114/114 in 368.78 seconds. Its longest container application passes
    in 145.48 seconds, the SystemC matrix passes in 61.13 seconds, and the main
    application passes in 24.27 seconds.
88. The independent exact-LLVM Release tree reconfigures and completes all 91
    affected build steps warning-clean with eight workers. Its full suite
    passes 114/114 in 312.67 seconds; the container application passes in
    105.82 seconds, the SystemC matrix in 57.23 seconds, and the main
    application in 22.47 seconds. Both full suites include all source, catalog,
    inventory, installed-public-contract, artifact, API/ABI, portability,
    release-candidate, application, and VHPI runtime gates.
89. `git diff --check` is clean and no merge-conflict paths exist. Close all
    twenty Batch 158 changes in exactly one accumulated commit and push. Do not
    run a sanitizer or inspect hosted CI because this is not a monitoring
    boundary. After that synchronized closeout, save and push the Batch 159
    restart plan before making any Batch 159 implementation change, then clear
    context and resume only from that plan.

## Batch 158 pre-implementation restart plan - 2026-08-07

1. Start in `/home/colin/projects/fsim`, read this file and the authoritative
   expanded Batch 158 allocation in `implementation_plan_v2.md`, and verify
   branch `codex/v2` is clean and synchronized at the documentation-only
   Batch 158 planning checkpoint. Its underlying pushed Batch 157 closeout is
   `8c33afe9c39846edf47ab01f12e678893a685c24`.
2. This checkpoint implements the required pre-batch flow. Batch 158 has been
   expanded into twenty exact changes without changing the locked VHPI scope or
   priority. The planning checkpoint is committed and pushed before any
   numbered implementation change, and the context that created it ends here.
   It is outside Changes 1-20 and does not consume the batch's single
   implementation commit.
3. No Batch 158 implementation file has been changed and Change 1 remains
   pending. Do not reuse conversational implementation detail from Batch 157.
   In the fresh context, verify the live Git state and reread the complete
   Change 1 contract before code discovery or editing.
4. Change 1 begins with the public VHPI C ABI foundation only: explicit host
   and plug-in version/size fields, pointer width, reserved flags, simulation
   identity, stable nonpointer handles, bounded diagnostic views, portable
   calling/export macros, and one exact bind symbol. Freeze its C layouts and
   negative validation before Change 2 introduces dynamic loading.
5. Reuse hardened platform and scheduler infrastructure where semantics agree,
   but keep VHPI identities, VHDL regions, selected/indexed names, constraints,
   nine-state values, foreign models, and restart rules distinct from VPI.
   Prefer the codebase knowledge graph for discovery. Use at least eight
   workers for builds.
6. Batch 158 is neither a sanitizer nor hosted-CI monitoring boundary. Keep
   Changes 1-19 in one recoverable accumulated worktree after the fresh context
   begins; do not commit or push implementation, run a sanitizer, or inspect
   hosted CI before Change 20.

## Batch 157 completed checkpoint - 2026-08-07

1. Start in `/home/colin/projects/fsim`, read this file and the authoritative
   Batch 157 allocation in `implementation_plan_v2.md`, and verify branch
   `codex/v2` remains based on pushed Batch 156 closeout `98407f3`. Preserve
   the dirty accumulated Batch 157 worktree described below; do not reset,
   commit, push, inspect hosted CI, or run a sanitizer before Change 20.
2. Batch 157 is expanded into twenty explicit changes. Change 1 is complete in
   the dirty worktree. The new public C ABI fixes explicit host and plug-in
   version/size fields, native pointer width, reserved flags, stable nonpointer
   64-bit handles, simulation identity, bounded diagnostic views, Windows/POSIX
   calling/export conventions, and the exact `fsim_vpi_plugin_bind_v1` symbol.
3. The C++ host constructor and validator reject incompatible versions,
   truncated tables, foreign pointer widths, reserved flags, zero simulation
   ownership, missing host context, and missing report callbacks. An independent
   C translation unit freezes the 40-byte host and 48-byte plug-in layouts and
   invokes the bounded diagnostic callback through the C table.
4. The affected exact-LLVM 22.1.8 Debug runtime target rebuilt warning-clean
   with eight workers. `fsim.runtime` and `fsim.source-line-budget` pass 2/2
   in 0.23 seconds, and `git diff --check` is clean.
5. Preserve this checkpoint and begin Change 2 by loading one VPI image through
   `platform::DynamicLibrary`, resolving and invoking the bind symbol
   transactionally, validating and owning the returned descriptor, containing
   bind/startup/shutdown failures, and guaranteeing exactly-once teardown. Do
   not commit, push, run a sanitizer, or inspect hosted CI before Change 20.
6. Change 2 is complete in the same dirty worktree. Loading validates the host
   before opening an image, resolves only the exact bind symbol, invokes bind
   and startup behind exception boundaries, and validates and copies the
   complete plug-in descriptor before publication. Open, missing-symbol, bind,
   descriptor, and startup failures release the provisional image without
   publishing plug-in or lifecycle state.
7. A successful load returns one move-only owner. Explicit shutdown and
   destructor teardown invoke the callback exactly once before image unload;
   the first status or exception result is retained for deterministic repeated
   inspection. A real hidden-visibility image covers successful startup,
   explicit and automatic shutdown, status failures, exceptions, malformed
   versions/sizes/flags/name/lifecycle, missing bind symbol, invalid host, and
   missing artifact.
8. The first focused build failed only because two test-only explicit result
   conversions were omitted. After that correction, the affected exact-LLVM
   Debug target rebuilt warning-clean with eight workers. `fsim.runtime` and
   `fsim.source-line-budget` pass 2/2 in 0.21 seconds, and `git diff --check`
   is clean.
9. Preserve this checkpoint and begin Change 3 with the simulation-owned error
   state and generation-qualified object-handle registry, including stable
   kind/parent identity and deterministic invalid, stale, released, and
   cross-simulation rejection. Do not commit, push, run a sanitizer, or inspect
   hosted CI before Change 20.
10. Change 3 is complete in the same dirty worktree. Mutex-safe last-error state
    owns bounded code/message storage, validates raw ABI severity before
    narrowing, remains inspectable until the next call boundary, and never
    borrows plug-in buffers. Invalid severity/code/message inputs reject without
    replacing valid owned state.
11. Object handles are 64-bit nonpointers encoding a process-unique registry,
    slot, and generation. Records preserve kind, parent, simple name,
    live-child count, and sibling uniqueness. Lookup and release distinguish
    malformed, cross-simulation, released, and stale handles; reuse advances the
    generation and parent release rejects while any child remains live.
12. Focused evidence covers two isolated simulations, root/module/net hierarchy,
    stable metadata, duplicate names, invalid kinds and parents, leaf-first
    release, released-before-reuse identity, stale-after-reuse identity, and
    owning error lifetime. The first final build correctly rejected a test-only
    out-of-range enum conversion under warnings-as-errors; accepting and
    validating the raw ABI integer before narrowing fixes the real boundary.
13. The corrected exact-LLVM Debug target builds warning-clean with eight
    workers. `fsim.runtime` and `fsim.source-line-budget` pass 2/2 in 0.21
    seconds, and `git diff --check` is clean. Preserve the accumulated
    worktree and begin Change 4 with deterministic name/hierarchy lookup,
    independently releasable relationship iterators, escaped/full/simple names,
    and source metadata. Do not commit, push, run a sanitizer, or inspect hosted
    CI before Change 20.
14. Change 4 is complete in the same dirty worktree. The object registry owns
    canonical simple and full names, normalizes escaped-name termination,
    supports exact full and parent-relative lookup, and retains optional
    file/line/column provenance. Invalid dotted simple names and incomplete
    source records reject before publication.
15. Child iteration captures live objects in creation order in a separate
    generation-qualified handle space. Iterators scan to explicit end, release
    independently, reject object/iterator confusion and cross-simulation use,
    and report released before reuse and stale after reuse. Object name maps,
    source records, and parent child counts remain synchronized across release.
16. Focused evidence covers an escaped module with a dotted identifier, its
    nested full name, exact relative lookup, source ownership, malformed names/
    sources, deterministic sibling order, iterator end/release/reuse, multi-root
    isolation, and final leaf-first hierarchy teardown. The first build required
    one test-only explicit iterator-result conversion.
17. The corrected exact-LLVM Debug target builds warning-clean with eight
    workers. `fsim.runtime` and `fsim.source-line-budget` pass 2/2 in 0.20
    seconds, and `git diff --check` is clean. Preserve the accumulated
    worktree and begin Change 5 with exact scalar object type/property metadata
    for modules, interfaces, programs, packages, ports, nets, variables,
    parameters, named events, and generated scopes. Do not commit, push, run a
    sanitizer, or inspect hosted CI before Change 20.
18. Change 5 is complete in the same dirty worktree. Typed records and queries
    preserve exact Verilog-2005/SystemVerilog-2017 ownership, scalar category,
    net kind, port direction, static/automatic lifetime, bounded width,
    signedness, and constant status for roots, modules, interfaces, programs,
    packages, generated scopes, ports, nets, variables, parameters, and events.
19. Validation rejects unknown enum values, Verilog ownership of
    SystemVerilog-only kinds, malformed scalar widths, signed real/string/event
    types, missing directions or net kinds, automatic nonvariables, and mutable
    parameters before hierarchy publication. Focused positives cover every
    allocated Change 5 kind and exact property reads; malformed profiles and
    two-simulation isolation are covered negatively.
20. The first build exposed one older aggregate fixture missing the newly
    explicit type field. After adding it, the affected exact-LLVM Debug target
    builds warning-clean with eight workers. `fsim.runtime` and
    `fsim.source-line-budget` pass 2/2 in 0.20 seconds, and `git diff --check`
    is clean. Preserve the accumulated worktree and begin Change 6 with recursive
    memory, array, queue, associative-array, struct, union, enum, string, class,
    class-property, and dynamic-object descriptors. Do not commit, push, run a
    sanitizer, or inspect hosted CI before Change 20.

21. Change 6 is complete in the same dirty worktree. Recursive semantic type
    descriptors now represent scalar, packed and unpacked fixed arrays, dynamic
    arrays, queues, associative arrays, structs, unions, enums, strings,
    classes, named class properties, and nominal class handles without byte
    offsets, native pointers, or other host-layout fields.
22. Checked recursive validation bounds depth, node count, fixed element count,
    fixed bits, names, queue/container maxima, and arithmetic. It rejects
    malformed shapes, duplicate member and enum identity, invalid associative
    keys, nonintegral or dynamic packed elements, overflowing ranges, language
    mismatches, and descriptor/object-kind mismatches before publication. The
    registry copies accepted descriptor trees into immutable owning snapshots.
23. Focused evidence covers ascending and descending packed ranges, fixed
    memories, nested dynamic records, queues, associative arrays, unions,
    enums, strings, classes/properties/handles, immutable snapshots, recursive
    limits, and malformed inputs. The affected exact-LLVM Debug target builds
    warning-clean with eight workers. `fsim.runtime` and
    `fsim.source-line-budget` pass 2/2 in 0.20 seconds, and
    `git diff --check` is clean. Preserve the accumulated worktree and begin
    Change 7 with checked scalar, integer, real, string, time, strength, vector,
    four-state, and nine-state value reads. Do not commit, push, run a sanitizer,
    or inspect hosted CI before Change 20.

24. Change 7 is complete in the same dirty worktree. The registry can bind
    one canonical runtime value to each readable object and perform checked,
    mutex-safe reads as scalar, raw integer bits, real/shortreal, bounded string,
    full-width time, scalar strength, two-state words, four-state aval/bval
    planes, or four-plane nine-state words. Value records remain owned by the
    simulation and are discarded on object release or generation reuse.
25. Conversion validates object identity, exact category and width, strength
    encodings, known-state requirements, and format compatibility. Caller-owned
    vector and string buffers receive required sizes first; undersized requests,
    mismatched types, unknown/lossy conversions, duplicate binding, invalid
    formats, and resource violations fail without partial writes. Released,
    stale, malformed, and cross-simulation handles retain distinct errors.
26. Focused evidence covers the 64/65-bit word boundary, exact X/Z aval/bval
    planes, U/W/L/H/don't-care nine-state planes, signed raw integer identity,
    unknown-state rejection, real and widened shortreal, embedded-NUL strings,
    full-width time, distinct zero/one strength ranks, immutable undersized
    buffers, ownership, and handle isolation. The first compile check corrected
    warning-only shadowed test names; the first focused gates corrected two
    test-only nine-state plane expectations. The final exact-LLVM Debug target
    builds warning-clean with eight workers. `fsim.runtime` and
    `fsim.source-line-budget` pass 2/2 in 0.20 seconds, and
    `git diff --check` is clean. Preserve the accumulated worktree and begin
    Change 8 with checked deposits, delayed writes, and force/release semantics.
    Do not commit, push, run a sanitizer, or inspect hosted CI before Change 20.

27. Change 8 is complete in the same dirty worktree. Reverse conversion
    transactionally accepts scalar, raw integer, real/shortreal, string, time,
    strength, two-state, aval/bval four-state, and four-plane nine-state input
    formats. It validates caller sizes, unused word padding, state encodings,
    exact type/width, embedded bytes, shortreal overflow, and strength ranks
    before constructing a canonical owned value.
28. Immediate deposits update the underlying value, force adds a separate
    visible layer, deposits continue beneath an active force, and release reveals
    the latest underlying value. Constants and input ports reject distinctly;
    output/inout ports and writable nets/variables accept exact values. A
    scheduler-backed controller preflights delayed requests, executes them in the
    common update phase with stable order, supports independent transport writes,
    inertial supersession, explicit cancellation, retained outcomes, and
    independent scheduled-handle release.
29. Focused evidence covers 65-bit input boundaries, invalid padding and
    nine-state encodings, embedded-NUL strings, shortreal overflow, strength
    deposits, deposit-under-force/release reveal, constant/input/width negatives,
    exact delayed deposit/force/release ticks, transport coexistence, inertial
    cancellation, explicit cancellation, time overflow, and stale-generation
    failure after object reuse. The affected exact-LLVM Debug target builds
    warning-clean with eight workers. `fsim.runtime` and
    `fsim.source-line-budget` pass 2/2 in 0.20 seconds, and
    tracked plus untracked whitespace checks are clean. Preserve the accumulated
    worktree and begin Change 9 with exact simulation-time queries and delay
    scheduling. Do not commit, push, run a sanitizer, or inspect hosted CI before
    Change 20.

30. Change 9 is complete in the same dirty worktree. A validated time
    profile retains exact decimal unit and precision exponents from seconds
    through femtoseconds. Integer queries expose full 64-bit scheduler ticks as
    high/low words; scaled-real queries convert ticks into the configured unit;
    every query also preserves the common scheduler's current delta identity.
31. Delay conversion combines integer high/low words exactly or maps nonnegative
    finite scaled-real units onto precision ticks with deterministic half-up
    rounding. Negative, nonfinite, foreign-format, profile, conversion, and
    current-time overflow reject before scheduling. Cancelable after-delay work
    maps to the common active phase with caller stable order, retained fired/
    cancelled/callback-failed status, exception containment, cross-service
    identity, independent handle release, bounded resources, and teardown
    cancellation.
32. Focused evidence covers nanosecond/picosecond scaling, 64-bit integer
    recombination, half-tick rounding, invalid profiles/formats, negative and
    infinite delays, identical-time stable order, exact query inversion, callback
    exception containment, explicit cancellation, service isolation, time
    overflow, active-to-next-delta identity, and destruction with pending work.
    The first build removed one stray test-only import; the first focused gate
    corrected an idle-scheduler delta setup. The final exact-LLVM Debug target
    builds warning-clean with eight workers. `fsim.runtime` and
    `fsim.source-line-budget` pass 2/2 in 0.19 seconds, and tracked plus
    untracked whitespace checks are clean. Preserve the accumulated worktree and
    begin Change 10 with value-change, after-delay, read-write, read-only,
    next-time, and synchronization-region callback registration and dispatch.
    Do not commit, push, run a sanitizer, or inspect hosted CI before Change 20.

33. Change 10 is complete in the same dirty worktree. A simulation-owned
    callback manager registers value-change, after-delay, read-write, read-only,
    next-time, and synchronization callbacks with generation-independent
    manager handles, exact registration order, optional object identity, copied
    user data, retained status, and the registry's multi-root simulation
    identity. Cross-simulation objects, invalid requests, absent future times,
    time-conversion failures, stable-order overflow, and bounded-resource
    failures reject before publication.
34. Synchronization callbacks map to update, value-change and read-write to
    reactive, read-only to postponed, and after-delay/next-time to active
    scheduler regions. The scheduler now exposes its earliest queued future
    time without consuming it. Registry value observers run only after a
    visible value is published and its mutex is unlocked; same-value deposits
    and deposits hidden beneath force are silent, while force and release queue
    copied visible values. One scheduled dispatch preserves change sequence and
    registration order and permits observer-side registry re-entry.
35. Focused evidence covers all six callback kinds, exact scheduler phase/time/
    delta, copied object/value/user-data/registration identity, two independent
    roots, cross-simulation rejection, missing next time, observer re-entry,
    duplicate-value suppression, force-hidden deposits, release visibility,
    persistent value-change status, one-shot status, and cross-manager handle
    isolation. The final exact-LLVM Debug target builds warning-clean with eight
    workers. `fsim.runtime` and `fsim.source-line-budget` pass 2/2 in 0.20
    seconds, and tracked plus new callback-file whitespace checks are clean.
    Preserve the accumulated worktree and begin Change 11 with lifecycle
    callbacks, callback removal/self-removal, nested registration, exception
    containment, and scheduler-safe re-entry. Do not commit, push, run a
    sanitizer, or inspect hosted CI before Change 20.
36. Change 11 is complete in the same dirty worktree. Callback kinds now cover
    start/end simulation and start/end reset, save, and restart boundaries.
    Lifecycle notification captures one active registration snapshot in exact
    registration order and queues start boundaries in the common active region
    and end boundaries in postponed. Every event retains exact kind, integer
    time/delta, user data, registration order, and simulation identity.
37. Removal first publishes retained removed state, then cancels any pending
    scheduler-backed or timed work outside the manager mutex. Self-removal and
    removal of a later callback take effect within the current snapshot; nested
    registrations wait for the next matching notification. Invocation occurs
    outside manager and registry locks, so nested registration, removal,
    lifecycle dispatch, and visible value publication re-enter safely.
    Exceptions become per-registration callback-failed status without aborting
    later callbacks, and reactive value observers see only fully published
    state.
38. Focused evidence covers all eight lifecycle boundaries, active/postponed
    mapping, exact time/user/simulation identity, self and peer removal, inactive
    removal, delayed-work cancellation, nested-registration deferral, contained
    exceptions, later-callback continuation, cross-manager removal, invalid
    lifecycle requests, lifecycle-to-lifecycle re-entry, and lifecycle-to-value
    reactive re-entry. The final exact-LLVM Debug target builds warning-clean
    with eight workers. `fsim.runtime` and `fsim.source-line-budget` pass 2/2
    in 0.20 seconds, and tracked plus new callback-file whitespace checks are
    clean. Preserve the accumulated worktree and begin Change 12 with stop,
    finish, reset, interactive control, force, and release operations at common
    scheduler safe points. Do not commit, push, run a sanitizer, or inspect
    hosted CI before Change 20.
39. Change 12 is complete in the same dirty worktree. A simulation-owned
    control service owns generation-independent operation handles, exact
    simulation identity, pending/applied/failed status, value errors, and
    Running, Stopped, Interactive, Reset, or Finished state. Invalid operation
    shapes, mistyped force values, foreign objects, inactive submission,
    controller-handle mismatch, stable-order overflow, and bounded-resource
    failures reject before scheduler publication.
40. Force and release execute in update and publish visible transitions through
    reactive callbacks. Stop and interactive execute at postponed safe points,
    preserve later work, and resume distinctly. Finish dispatches end-of-
    simulation callbacks before a nonresumable terminal stop. Reset dispatches
    start-reset, restores every live bound object from an owned initial snapshot
    while clearing force layers transactionally, publishes exact visible changes,
    dispatches end-reset, and enters resumable reset state. Reset resume cancels
    old scheduler work, rewinds time/delta/sequence identity, and clears stop.
41. Focused evidence covers ordered force/release values, cross-simulation and
    malformed/type preflight, retained stop work and resume, interactive resume,
    operation status/ownership, start-reset/value/end-reset ordering, force
    clearing, initial-value restoration, future-work discard, scheduler rewind,
    end-of-simulation ordering, and terminal finish rejection. The final exact-
    LLVM Debug target builds warning-clean with eight workers. `fsim.runtime`
    and `fsim.source-line-budget` pass 2/2 in 0.20 seconds, and tracked plus
    new control-file whitespace checks are clean. Preserve the accumulated
    worktree and begin Change 13 with transactional system task/function
    registration and compiletf/sizetf/calltf execution. Do not commit, push, run
    a sanitizer, or inspect hosted CI before Change 20.
42. Change 13 is complete in the same dirty worktree. A simulation-owned system
    callable registry accepts only bounded `$identifier` names, supported task
    or function kinds, mandatory compiletf/calltf callbacks, and internally
    consistent sizetf plus exact return profiles. Task registrations reject
    return metadata or sizetf, function registrations require both, and all
    validation plus stable pre-move key construction completes before the map
    publishes an entry. Duplicate, malformed, invalid-kind, invalid-callback,
    and allocation failures leave registration count and prior entries intact.
43. Invocation snapshots the registration and releases the registry lock before
    user code. It resolves the supplied handle through the same simulation's
    VPI object registry, accepts only real scope kinds, owns arguments in their
    original order, and presents exact callable kind, name, optional return
    type, full scope identity, and an immutable argument span to every phase.
    Functions run compiletf, sizetf, then calltf and require the reported width
    to equal the registered return width; tasks run compiletf then calltf with
    no return profile. Rejections stop later phases and retain bounded owned
    diagnostics. Standard and nonstandard exceptions in compiletf, sizetf, or
    calltf become phase-specific callback-exception results; best-effort
    diagnostic ownership itself cannot reopen the callback boundary.
44. Focused evidence covers exact function and task phase order, exact return
    type/width, hierarchical scope identity, mixed argument order, transactional
    duplicate and malformed profile rejection, missing/foreign/non-scope
    invocation rejection, sizetf rejection and width mismatch without calltf,
    owned diagnostics, and exception containment at all three callback phases.
    The final exact-LLVM Debug target builds warning-clean with eight workers.
    `fsim.runtime` and `fsim.source-line-budget` pass 2/2 in 0.22 seconds;
    tracked and all three new system-call files pass whitespace checks.
    `clang-format` is not installed in this environment, so its non-mutating
    dry-run was unavailable. Preserve the accumulated worktree and begin Change
    14 with system-call/argument handles, typed results, registration/call user
    data, nested/reentrant calls, late-registration diagnostics, and
    deterministic teardown. Do not commit, push, run a sanitizer, or inspect
    hosted CI before Change 20.
45. Change 14 is complete in the same dirty worktree. Registration and call
    handles carry one registry owner plus monotonic identity; argument handles
    additionally carry their call and one-based ordinal. Each retained call
    owns its registration snapshot, full invocation scope, immutable ordered
    values and argument handles, per-call user data, optional typed result,
    current/final phase, execution error, and active/completed/failed state.
    Invocation contexts expose registration, call, and argument handles plus
    independent per-registration and per-call user data to every callback.
46. Result publication is admitted only during calltf for an active function
    and validates the exact registered return type. Early, task, wrong-type,
    duplicate, and missing publications reject distinctly. Callbacks still run
    outside locks, so a calltf can synchronously execute a different callable
    or the same registration without aliasing call records. Explicit sealing
    gives owned late-registration diagnostics; unregister removes future name
    lookup while retained calls remain queryable. Completed/failed calls release
    explicitly and stale their argument handles; active release and
    cross-registry registration/call/argument handles reject.
47. Teardown is idempotent, closes and seals the registry, clears calls before
    registrations, invalidates all retained surfaces, and when invoked from
    compiletf prevents sizetf and calltf from running. Focused evidence covers
    active/post-call metadata, ordered argument lookup, exact user data, typed
    publication and wrong/duplicate/missing results, nested and same-
    registration reentry, active-release refusal, explicit release/unregister,
    duplicate/late diagnostics, cross-owner handles, closed surfaces, and
    callback-initiated teardown. The final exact-LLVM Debug target builds
    warning-clean with eight workers. `fsim.runtime` and
    `fsim.source-line-budget` pass 2/2 in 0.21 seconds, and tracked plus all
    three system-call files pass whitespace checks. Preserve the accumulated
    worktree and begin Change 15 with MCD allocation/control, vlog and formatted
    output, argv/product/version identity, severity routing, bounded strings,
    file ownership, and cross-platform descriptors. Do not commit, push, run a
    sanitizer, or inspect hosted CI before Change 20.
48. Change 15 is complete in the same dirty worktree. A simulation-owned VPI
    I/O service fixes the portable 32-bit descriptor layout independently of
    native Windows/POSIX widths: bit zero selects standard output, bits 1-30
    identify monotonic MCD channels, and the high bit tags monotonic file
    descriptors. Every public handle also carries service ownership.
    Combination validates all selected channels before fan-out; file
    descriptors cannot enter MCDs; flush, composite close, stale detection,
    append, cross-service rejection, and open-file counts preserve exact file
    ownership.
49. File opens accept only bounded write/append modes and root-relative UTF-8
    paths that cannot escape the canonical configured root. Vlog routes through
    an injected output sink; note, warning, error, and fatal diagnostics route
    through an independent sink. Both contain exceptions outside the service
    lock. Formatting owns one bounded message, consumes ordered `{}`
    placeholders, supports escaped braces, and rejects malformed, missing,
    excess, or oversized arguments. Configuration owns bounded command-line
    argv, product identity, and the canonical `fsim::version` default.
50. Focused evidence covers exact standard-output/MCD/file-descriptor values,
    MCD combination and fan-out, vlog, flush, composite close, append, monotonic
    identity, stale/cross-owner handles, all severity routes, escaped formatting,
    malformed counts/severity/path/mode/bounds, sink exceptions, zero simulation
    rejection, and idempotent teardown with retained file content. The final
    exact-LLVM Debug target builds warning-clean with eight workers.
    `fsim.runtime` and `fsim.source-line-budget` pass 2/2 in 0.20 seconds,
    and tracked plus all three new I/O files pass whitespace checks. Preserve
    the accumulated worktree and begin Change 16 with save/restart and artifact
    preservation or explicit invalidation of registrations, callbacks, user
    data, handles, forced values, and plug-in provenance. Do not commit, push,
    run a sanitizer, or inspect hosted CI before Change 20.
51. Change 16 is complete in the same dirty worktree. The object registry now
    exports a complete typed snapshot of every live value-bearing object,
    including its source handle, canonical full name, underlying value, and
    force layer. Restore first validates complete one-to-one object inventory,
    unique source handles and names, deep type identity, and both stored values,
    then applies all records in one mutex-held transaction and returns an exact
    source-to-target handle map. Missing, omitted, duplicate, or mistyped
    records reject before any target mutation.
52. A public checkpoint contract distinguishes same-process restart from
    portable artifact reload. Restart requires the original simulation and
    unchanged callback, system-registration/call, and descriptor inventory; it
    preserves exact object and external handles, callback closures, callback
    and system user data, registrations, retained calls, descriptors, and
    forced values. Portable reload requires an empty external target, validates
    schema, both VPI ABI versions, design content, native cache, and ordered
    plug-in path/name/content/host provenance before object restoration, remaps
    object handles by full name, and returns counted reasons invalidating native
    closures, pointer user data, system registrations/calls/arguments, open
    streams, and dynamic-library contexts for verified re-registration.
53. Focused evidence covers exact-handle restart, callback and system user-data
    execution after restart, retained descriptor writes, underlying and forced
    values, cross-registry handle remapping, schema/host-ABI/plug-in-ABI/content/
    cache/plug-in mismatch rejection, missing and omitted records, seven
    explicit portable invalidation classes, and unchanged target state after
    every rejected artifact. The final exact-LLVM Debug target builds warning-
    clean with eight workers. The full runtime executable passes;
    `fsim.runtime` and `fsim.source-line-budget` pass 2/2 in 0.22 seconds.
    Tracked and all three new checkpoint files pass whitespace checks. Preserve
    the accumulated worktree and begin Change 17 with independently compiled C
    and C++ reference plug-ins. Do not commit, push, run a sanitizer, or inspect
    hosted CI before Change 20.
54. Change 17 is complete in the same dirty worktree. The public 40-byte v1 host
    layout and existing bind entry point remain unchanged. A separate 56-byte
    v2 wrapper begins with the complete v1 table and appends one service context
    plus one C-compatible invocation callback. Its bounded request/result
    records carry explicit sizes, operation/flags, stable integer handle,
    argument, user data, and sized text; operations allocate hierarchy, value,
    time, callback, control, system-task, system-function, I/O, user-data, and
    lifecycle identities without exposing C++ owners across the image boundary.
55. Independently compiled C and C++ shared libraries include only the public
    ABI header and export the exact bind symbol through the common Windows/POSIX
    calling and visibility macros. Each requires a validated v2 service host,
    invokes all ten service families in order during startup with its own exact
    user-data base, and emits one flagged lifecycle request during exactly-once
    shutdown. A reporting-only v1 host is rejected without startup.
56. Focused evidence freezes the 40-byte v1 prefix, 56-byte v2 wrapper, and
    portable request/result layouts; validates missing v2 context/callback
    rejection; and checks both plug-in names, normalized paths, operation order,
    arguments, user data, sized text, lifecycle flags, and idempotent shutdown.
    The final exact-LLVM Debug target builds warning-clean with eight workers.
    The full runtime executable passes; `fsim.runtime` and
    `fsim.source-line-budget` pass 2/2 in 0.22 seconds. Tracked and all three
    new reference-plug-in files pass whitespace checks. Preserve the accumulated
    worktree and begin Change 18 with malformed ABI/profile, lifecycle,
    handle/iterator/callback/reentrant/exception/resource/post-unload negative
    matrices. Do not commit, push, run a sanitizer, or inspect hosted CI before
    Change 20.
57. Change 18 is complete in the same dirty worktree. The accumulated runtime
    negative matrix covers malformed v1/v2 host and plug-in ABI versions, sizes,
    flags, pointer widths, contexts and callbacks; missing artifacts/bind
    symbols; bind/startup/shutdown status and exception paths; and failure
    containment without publishing provisional images.
58. Object, iterator, value, time, callback, control, system-call, and I/O
    evidence distinguishes invalid, stale, released, and cross-simulation/
    cross-owner handles. It also covers callback self/peer removal, nested
    registration and lifecycle/value reentry, same/cross-callable recursive
    system calls, user exceptions, bounded resources, callback-initiated
    teardown, and every closed surface. New reference-boundary cases reject
    null ownership/requests, truncated request/result sizes, invalid operation,
    missing sized text, host resource failure, malformed successful result, and
    stale post-unload access. Both independent C and C++ images fail startup
    without partial publication; explicit unload produces no later host call.
59. The final exact-LLVM Debug target builds warning-clean with eight workers.
    The full runtime executable passes; `fsim.runtime` and
    `fsim.source-line-budget` pass 2/2 in 0.22 seconds. Preserve the dirty
    accumulated worktree and begin Change 19 with interpreter/LLVM O0/O2,
    multi-root, cold/warm cache, standalone object/design, mapped-library,
    relocation, save/restart differentials, and public VPI documentation/
    inventories. Do not commit, push, run a sanitizer, or inspect hosted CI
    before Change 20.
60. Change 19 is complete in the same dirty worktree. The independent C and
    C++ images now produce exact matching hierarchy/value/time/callback/
    control/system-task/system-function/I/O/user-data/lifecycle transcripts
    across interpreter, compiled O0, and compiled O2 labels. Distinct
    simulation identities remain isolated; repeated loads exercise the warm
    path; copied images load and shut down exactly once from relocated paths.
61. The checkpoint matrix retains callback/system user data, exact handles,
    calls, descriptors, and forced values for same-process restart. Portable
    restore validates schema, both ABI versions, content/cache identity,
    ordered image provenance, complete object inventory, deep types, and every
    value before mutation; it remaps canonical object paths and returns seven
    counted native-state invalidations. The existing `.fsimobj`, `.fsimdesign`,
    mapped-library, cache, application engine/artifact, and multi-root API
    owners pass 6/6 in 25.34 seconds.
62. Public README, architecture, language support, diagnostics, the new
    `systemverilog-vpi.md` guide/buildable C/C++ examples, feature rows
    `SV-781` through `SV-790`, test inventory, and every frozen release audit
    are synchronized. The candidate now freezes 1,220 execute rows, 4,880
    evidence cells, 461 exact paths (201 test, 244 production, 16 release), 125
    runtime owners, 1,989 diagnostics, 652 bounded sources, 744 authored
    artifacts, and 244 test/control files. The reviewed matrix SHA-256 is
    `f60cf9a97f96315f5068a3047e113f70405b9cead25f9e379e5d556b22f76c51`;
    the evidence-path SHA-256 is
    `d3e9c14916fe0ec37156679632dd3b5488936f94f81d63a02ea5f164b81c51bd`.
63. The final exact-LLVM Debug runtime target builds warning-clean with eight
    workers and the full runtime executable passes. The nine catalog, source,
    legality, SystemVerilog, differential, public, inventory, candidate, and
    installed-public-contract gates pass 9/9 in 14.01 seconds;
    `git diff --check` is clean. Preserve the accumulated dirty worktree and
    begin Change 20 with full non-sanitized exact-LLVM Debug and Release builds,
    regressions, and release gates. Then commit and push exactly once. Do not
    run a sanitizer or inspect hosted CI because Batch 157 is not a monitoring
    boundary.
64. Change 20 is complete. The first exact-LLVM Release build exposed an
    optimizer-only GCC `maybe-uninitialized` error while moving a delayed VPI
    write's `optional<variant>` into the scheduled apply helper. The delayed
    path now copies into an explicitly initialized stored value and carries a
    separate presence flag before passing a pointer to the transactional apply
    helper. This preserves deposit/force/release semantics and removes the
    optimizer ambiguity. Focused Debug and Release runtime targets rebuild
    warning-clean and both runtime executables pass.
65. The final exact-LLVM Debug tree relinks all 20 affected executables against
    the repaired runtime warning-clean with eight workers and passes the full
    suite 114/114 in 373.55 seconds. The independent Release tree completes the
    remaining 338 full-build steps warning-clean with eight workers and passes
    114/114 in 300.77 seconds. Both suites include the source, catalog,
    inventory, installed-public-contract, artifact, API/ABI, portability,
    release-candidate, and VPI runtime gates. `git diff --check` is clean.
66. Close Batch 157 with exactly one commit and push. Do not run a sanitizer or
    inspect hosted CI because this is not a monitoring boundary. After the
    synchronized push, begin Batch 158 by expanding its locked VHPI allocation
    into twenty exact changes and record the new clean base before making the
    next accumulated worktree dirty.

## Batch 156 completed checkpoint - 2026-08-06

1. Start in `/home/colin/projects/fsim`, read this file and the authoritative
   Batch 156 allocation in `implementation_plan_v2.md`, and verify branch
   `codex/v2` remains based on pushed Batch 155 closeout `5a857d1`. Preserve
   the dirty accumulated Batch 156 worktree described below; do not reset,
   commit, push, inspect hosted CI, or run a sanitizer before Change 20.
2. Batch 156 is expanded into twenty explicit changes. Change 1 is complete in
   the dirty worktree. `ParsedDesign` owns compilation-unit DPI imports and
   exports, while packages, modules, interfaces, and programs own local DPI
   declarations. Every record retains direction, explicit owner kind and
   identity, all tokens through the terminating semicolon, and the combined
   source span independently of parser-token lifetime.
3. A dedicated `verilog_parser_dpi.cpp` selects only `import`/`export` followed
   by a string literal, preserving the existing package import/re-export path.
   Focused evidence covers both directions, every supported owner, balanced
   function profiles, exact source identity, and coexistence with package
   wildcard import/export. `FSIM-SV-PARSE-324` rejects a declaration missing
   its semicolon.
4. The affected exact-LLVM Debug frontend dependency graph builds
   warning-clean with eight workers. `fsim.frontend`,
   `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
   `git diff --check` is clean. Preserve this checkpoint and begin Change 2 by
   structuring the link string, qualifier, callable kind, SystemVerilog name,
   and optional C alias without reparsing source text. Do not run a sanitizer
   or inspect hosted CI because Batch 156 is not a monitoring boundary.
5. Batch 156 Change 2 is complete in the same dirty worktree. Each declaration
   now structures the normalized link name, `pure`/`context` qualifier,
   function/task kind, SystemVerilog callable name, and optional C identifier
   alias while retaining a separate owning token for every component. The
   alias path follows `c_identifier = function|task` for both directions.
6. Stable diagnostics require the `"DPI-C"` link string and reject missing
   callable kinds or names. The first focused run found that code 217 was
   already assigned to coverage options and that the missing-name negative
   inferred the `int` return keyword as a name; use the next free semantic code
   222 and exclude keyword candidates. The complete 72-step affected Debug
   graph rebuilds warning-clean with eight workers, the final three focused
   gates pass, and `git diff --check` is clean.
7. Preserve this checkpoint and begin Change 3 by structuring function return
   types and function/task formal profiles, including directions, types,
   dimensions, defaults, and exact profile tokens/spans. Do not commit, push,
   run a sanitizer, or inspect hosted CI before Change 20.
8. Batch 156 Change 3 is complete in the same dirty worktree. Imported
   functions own exact return-type tokens. Imported functions and tasks own
   complete formal/profile tokens and spans plus ordered formal records with
   direction, const-ref policy, type, name, unpacked dimensions, defaults, and
   their original token/span provenance. Exports intentionally remain
   name-only until scope resolution.
9. Stable parser diagnostics reject incomplete, unbalanced, or trailing
   profiles. Semantic diagnostics reject export qualifiers, pure tasks,
   non-input pure-function formals, nonportable C aliases, and formal defaults.
   Focused positive evidence covers packed and unpacked dimensions plus all
   planned direction forms. The complete 72-step affected Debug graph is
   warning-clean with eight workers; the three focused gates pass and
   `git diff --check` is clean.
10. Preserve this checkpoint and begin Change 4 by resolving imports and
    exports in their exact owner scope, rejecting duplicates and C/SV name
    conflicts, and publishing stable validated profiles for marshalling and
    callbacks. Do not commit, push, run a sanitizer, or inspect hosted CI
    before Change 20.
11. Batch 156 Change 4 is complete in the same dirty worktree. DPI imports and
    exports resolve within their exact compilation-unit or design-unit owner.
    Successful records publish the effective C linkage name and a validation
    state that preserves both structure and resolution failures. Resolved
    exports additionally own typed native function/task profiles with return,
    formal, reference, and callable-span data for callback lowering.
12. Compilation-unit native definitions are parsed only when a preceding
    same-kind DPI export introduces their name. The first focused run showed
    that globally accepting unqualified functions removed the stable
    `FSIM-SV-SEM-172` class-method negative; the narrowed path restores that
    compatibility contract while retaining compilation-unit DPI exports.
    Owner-local duplicate names/linkage, native/import conflicts, and unknown
    or wrong-kind exports have stable `FSIM-SV-SEM-228` through `230`
    diagnostics.
13. The complete 72-step affected Debug graph and the final 19-step correction
    build are warning-clean with eight workers. Frontend, diagnostic-catalog,
    and source-line-budget gates pass 3/3, and `git diff --check` is clean.
    Preserve the dirty worktree and begin Change 5 with scalar two-/four-state
    DPI value descriptors and checked import marshalling. Do not commit, push,
    run a sanitizer, or inspect hosted CI before Change 20.
14. Batch 156 Change 5 is complete in the same dirty worktree. Public runtime
    DPI scalar marshalling uses bounded owning 32-bit aval/bval planes for
    two-state and four-state payloads. It preserves X/Z and arbitrary word
    boundaries, rejects unknown two-state inputs, and validates output width,
    plane sizes, unused high bits, empty values, and the default 1,048,576-bit
    resource limit before constructing runtime storage.
15. Focused evidence covers a 65-bit four-state round-trip, exact two-state
    encoding, X/Z rejection, malformed planes, dirty high bits, width mismatch,
    and bounded failure. The first focused build required explicit construction
    of empty `PackedLogic4` error results; the corrected runtime target builds
    warning-clean with eight workers. `fsim.runtime` and
    `fsim.source-line-budget` pass 2/2, and `git diff --check` is clean.
16. Preserve this checkpoint and begin Change 6 with exact real-family bits,
    UTF-8 string storage, and chandle identity/lifetime marshalling. Do not
    commit, push, run a sanitizer, or inspect hosted CI before Change 20.
17. Batch 156 Change 6 is complete in the same dirty worktree. Direction-aware
    shortreal, real, and realtime payloads preserve exact IEEE bits including
    negative zero and reject nonfinite values, kind mismatch, dirty binary32
    high bits, and input writeback. String payloads retain strict UTF-8 bytes
    while rejecting embedded NUL, invalid encoding, direction mismatch, and
    bounded-size excess.
18. Chandle payloads preserve stable registry identities and explicit borrowed
    input versus writable output/inout/ref state. Released/stale handles,
    borrowed writeback, and direction mismatches reject before use. The first
    test build used an obsolete four-field registry descriptor initializer;
    matching the current three-field contract is warning-clean. Runtime and
    source-line-budget gates pass 2/2, and `git diff --check` is clean.
19. Preserve this checkpoint and begin Change 7 with recursive fixed-array,
    struct, and enum descriptors, checked layout/resource accounting, and
    direction-aware writeback. Do not commit, push, run a sanitizer, or inspect
    hosted CI before Change 20.
20. Batch 156 Change 7 is complete in the same dirty worktree. Recursive
    scalar, fixed-array, struct, and enum descriptors produce one canonical
    flattened leaf order and checked layout. Validation covers exact descriptor
    shape, unique struct members/enum values, enum representability, arithmetic
    overflow, nesting depth, 65,536 leaves, 1,048,576 total bits, and actual
    aval/bval payload bytes.
21. Direction-aware composite marshalling reuses the checked Change 5 scalar
    planes per leaf and rejects width/count, enum, descriptor, direction,
    unknown-value, and resource mismatches before writeback. Nested
    array-of-struct-with-enum positive and negative evidence passes. The
    13-step affected runtime build is warning-clean with eight workers;
    runtime/source gates pass 2/2, and `git diff --check` is clean.
22. Preserve this checkpoint and begin Change 8 with standard open-array range,
    index, element, contiguity, and lifetime-epoch behavior. Do not commit,
    push, run a sanitizer, or inspect hosted CI before Change 20.
23. Batch 156 Change 8 is complete in the same dirty worktree. Registry-owned
    open-array handles retain transfer mode, contiguity, and a slot/epoch
    identity. Dimensions preserve declared left/right direction and derive
    checked low/high/increment/size values; mixed-direction multidimensional
    indices map into canonical row-major flattened element leaves.
24. Creation reuses Change 7 descriptor layout and rejects empty, overflowing,
    resource-excessive, or value-count-mismatched shapes. Element access and
    writeback reject rank/bounds, direction, leaf-count, noncontiguous, and
    released/stale-handle misuse. Focused evidence covers a descending by
    ascending two-dimensional byte array, first/last lookup, inout writeback,
    contiguous copies, a noncontiguous input, and post-release epoch failure.
    The 14-step affected runtime build is warning-clean with eight workers;
    runtime/source gates pass 2/2, and `git diff --check` is clean.
25. Preserve this checkpoint and begin Change 9 with simulation-owned
    `svScope` identity, current-scope lookup, exact named-scope resolution, and
    scheduler-safe set/restore behavior. Do not commit, push, run a sanitizer,
    or inspect hosted CI before Change 20.
26. Batch 156 Change 9 is complete in the same dirty worktree. Scope handles
    contain the stable simulation identity, slot, and epoch. The registry owns
    canonical full names and parent identities and supports exact name, parent,
    and validity lookup while rejecting zero simulation identity, malformed or
    duplicate names, wrong parents, and cross-simulation handles.
27. Current scope belongs to an explicit scheduler execution context, never
    process-global or thread-local state. A successful set returns the prior
    handle for exact nested restoration; invalid sets leave state unchanged.
    Focused roots/nested/generated-name, two-context, restore, malformed-tree,
    and cross-simulation evidence passes. The 12-step affected runtime build is
    warning-clean with eight workers; runtime/source gates pass 2/2, and `git
    diff --check` is clean.
28. Preserve this checkpoint and begin Change 10 with the standard open-array
    query, dimension, pointer, element, and writeback façade over Change 8's
    registry-owned handles. Do not commit, push, run a sanitizer, or inspect
    hosted CI before Change 20.
29. Batch 156 Change 10 is complete in the same dirty worktree. Standard-style
    dimension/range, whole-storage, and indexed-element accessors return
    transient pointers into registry-owned flattened leaves with explicit
    counts. Read-only pointers permit input and writable arguments; mutable
    pointers require output/inout/ref direction. Whole-storage access requires
    contiguity, while indexed elements remain available for noncontiguous
    handles.
30. Rank, bounds, direction, contiguity, and released/stale failures return no
    pointer. Focused evidence covers dimension/range queries, first/last
    pointers, direct mutable publication, noncontiguous element access, input
    protection, and post-release invalidation. The six-step affected runtime
    build is warning-clean with eight workers; runtime/source gates pass 2/2,
    and `git diff --check` is clean.
31. Preserve this checkpoint and begin Change 11 with disabled-state helpers
    and exported callback dispatch carrying explicit current scope, argument
    direction, and exception boundaries. Do not commit, push, run a sanitizer,
    or inspect hosted CI before Change 20.
32. Batch 156 Change 11 is complete in the same dirty worktree. Exported
    callbacks register by exact linkage name, validated Change 9 scope, ordered
    direction profile, and owning callable. Dispatch installs the callback
    scope in the explicit execution context, invokes against a transactional
    owning frame, publishes output/inout/ref values only on success, and
    restores the prior scope on every normal or exceptional path.
33. Inputs expose read-only access; mutable input and out-of-range access become
    typed failures. Pending disabled state must be observed and acknowledged or
    publication rejects while retaining the pending state. Standard and
    nonstandard C++ exceptions are contained with rejected outputs. Focused
    success, scope restoration, input-write, acknowledged/unacknowledged
    disable, exception rollback, unknown-name, arity, and duplicate evidence
    passes. The first test compile found one excess vector dereference; the
    corrected four-step affected runtime build is warning-clean with eight
    workers. Runtime/source gates pass 2/2, and `git diff --check` is clean.
34. Preserve this checkpoint and begin Change 12 with suspending imported
    tasks, callback re-entry, and scheduler containment across normal return,
    suspension, cancellation, disable, and failure. Do not commit, push, run a
    sanitizer, or inspect hosted CI before Change 20.
35. Batch 156 Change 12 is complete in the same dirty worktree. Imported tasks
    bind exact linkage, Change 9 scope, directions, and an owning callable to
    the existing deterministic scheduler. Each generation-qualified invocation
    owns a cancelable scheduler handle plus address-stable transactional
    argument state retained privately across timed suspensions. Writable values
    publish only on final completion.
36. Resume reinstalls the task scope; nested exported callbacks install and
    restore their own scope, then the task restores its caller. Cancellation
    removes the pending resume and unpublished values. Direction/arity,
    disabled state, cross-simulation handles, scheduling errors, exceptions,
    and registry destruction remain contained without poisoning later work.
    Focused suspension/resume, private intermediate state, callback re-entry,
    scope restore, cancel, exception, later-work survival, and malformed-use
    evidence passes. The first compile required an explicit boolean conversion;
    invocation storage was then made address-stable for re-entry. The corrected
    five-step affected runtime build is warning-clean with eight workers;
    runtime/source gates pass 2/2, and `git diff --check` is clean.
37. Preserve this checkpoint and begin Change 13 with a versioned portable DPI
    plug-in manifest and deterministic discovery/build/link contract. Do not
    commit, push, run a sanitizer, or inspect hosted CI before Change 20.
38. Batch 156 Change 13 is complete in the same dirty worktree. A versioned
    manifest owns exact source, include, library, import-symbol, and
    export-symbol order. It rejects unsupported versions, invalid identifiers,
    absolute/parent-escaping paths, unsupported source extensions, duplicates,
    and import/export symbol collisions.
39. Deterministic planning emits argv vectors rather than shell text. Source
    ordinals give collision-free stable object paths. POSIX uses C++20/PIC/
    hidden/shared options; MSVC uses explicit C++20/EH/compile/object/DLL/output
    options. Discovery preserves root order and uses `plugin.so` or
    `plugin.dll`. Repeatability, two-source POSIX, MSVC, path escape, symbol
    collision, and version evidence passes. The 12-step affected runtime build
    is warning-clean with eight workers; runtime/source gates pass 2/2, and
    `git diff --check` is clean.
40. Preserve this checkpoint and begin Change 14 by loading the planned
    artifact through `platform::DynamicLibrary` and resolving the complete
    exact import/export symbol inventory transactionally. Do not commit, push,
    run a sanitizer, or inspect hosted CI before Change 20.
41. Batch 156 Change 14 is complete in the same dirty worktree. A move-only
    loaded DPI plug-in owns the existing hardened platform dynamic-library
    handle, normalized artifact path, and separate exact import/export address
    maps. Every declared symbol resolves before publication; open or symbol
    failure destroys the provisional library and returns no partial state.
42. A real hidden-visibility shared-library fixture explicitly exports
    `dpi_add` and `sv_report`. Focused loading resolves both inventories,
    rejects cross-map lookup, and proves a missing symbol rejects
    transactionally. The first compile exposed a missing `unordered_map`
    include; the corrected 13-step affected target builds warning-clean with
    eight workers. Runtime/source gates pass 2/2, and `git diff --check` is
    clean.
43. Preserve this checkpoint and begin Change 15 with the versioned C ABI
    descriptor, struct-size checks, relocatable manifest identity, artifact
    content provenance, and complete cache key. Do not commit, push, run a
    sanitizer, or inspect hosted CI before Change 20.
44. Batch 156 Change 15 is complete in the same dirty worktree. A C-compatible
    ABI-v1 descriptor reports version, fixed-prefix size, pointer width,
    reserved flags, and length-delimited plug-in name through explicit export
    and calling-convention macros. Loading validates it before resolving any
    declared callable and rejects missing/null/throwing descriptors or every
    field mismatch without publishing a plug-in.
45. SHA-256 provenance separately covers canonical relative manifest fields
    and complete artifact bytes. The cache key adds toolchain identity,
    platform, ABI version, and pointer width while excluding producer roots.
    The real fixture plus version/pointer-width negatives, stable 64-hex
    digests, repeatability, artifact equality, and toolchain divergence pass.
    The six-step affected runtime/fixture build is warning-clean with eight
    workers; runtime/source gates pass 2/2, and `git diff --check` is clean.
46. Preserve this checkpoint and begin Change 16 with leased symbol lifetime,
    normal unload, explicit failure quarantine, and complete Windows/POSIX
    export/calling-convention behavior. Do not commit, push, run a sanitizer,
    or inspect hosted CI before Change 20.
47. Batch 156 Change 16 is complete in the same dirty worktree. The shared C
    header uses `__declspec(dllexport)` plus explicit `__cdecl` on Windows and
    default visibility on POSIX; generated MSVC plans explicitly add `/Gd`.
    Symbol lookups return leases holding shared module ownership, so addresses
    survive facade destruction. Normal release unloads after the final lease.
48. A thread-safe explicit quarantine intentionally grants process-lifetime
    module ownership when external registrations may retain addresses. The real
    fixture is invoked through its leased calling-convention type after facade
    destruction; focused evidence observes normal unload after lease release
    and residency after quarantine. The first compile required a mutable loader
    result for explicit release. The corrected three-step affected build is
    warning-clean with eight workers; runtime/source gates pass 2/2, and `git
    diff --check` is clean.
49. Preserve this checkpoint and begin Change 17 with independent C and C++
    plug-in fixtures plus actual malformed ABI/symbol and profile negatives.
    Do not commit, push, run a sanitizer, or inspect hosted CI before Change 20.
50. Batch 156 Change 17 is complete in the same dirty worktree. One real
    hidden-visibility plug-in combines independently compiled C and C++
    translation units using the same public C ABI header. Both exports resolve,
    retain the module through leases, and execute through the explicit calling
    convention with distinct expected results.
51. A second real shared library exposes the expected callable names but
    advertises ABI version 2; loading rejects it before symbol publication.
    Together with missing-symbol, callback/task arity, mutable-input,
    descriptor-field, and manifest symbol negatives, this closes malformed
    boundary evidence. The 21-step affected runtime/C/C++ fixture build is
    warning-clean with eight workers; runtime/source gates pass 2/2, and `git
    diff --check` is clean.
52. Preserve this checkpoint and begin Change 18 with interpreter, compiled
    O0/O2, multi-root, multi-context, suspension, and callback-re-entry
    differentials through the real fixture. Do not commit, push, run a
    sanitizer, or inspect hosted CI before Change 20.
53. Batch 156 Change 18 is complete in the same dirty worktree. The real
    `dpi_add` symbol executes through an interpreter-style owning scalar
    marshal/unmarshal path and compiled O0/O2 call paths with identical value
    42. Two simulation registries and current-scope contexts invoke the same
    leased symbol without scope or identity leakage, and a scheduler-backed
    imported task resumes through that symbol after suspension.
54. The ten-step affected build is warning-clean with eight workers. Runtime
    and source-line-budget gates pass 2/2, and `git diff --check` is clean.
55. Change 19 synchronizes the bounded public contract in README,
    `architecture.md`, `language-support.md`, and the new
    `systemverilog-dpi.md`. Feature rows `SV-771` through `SV-780` bind every
    supported declaration, value, composite, open-array, scope, callback,
    task, plug-in, ABI/lifetime, provenance, and differential boundary to its
    positive, negative, implementation, and runtime evidence. Unrestricted DPI
    profiles/extensions, VPI/VHPI, and UVM remain explicitly deferred.
56. The affected exact-LLVM Debug frontend/runtime targets are warning-clean
    with eight workers (`ninja: no work to do`). `fsim.frontend`,
    `fsim.runtime`, `fsim.diagnostics-catalog`, and
    `fsim.source-line-budget` pass 4/4 in 0.39 seconds, and `git diff --check`
    is clean. Preserve all accumulated Batch 156 edits and begin Change 20
    with the full non-sanitized exact-LLVM Debug and Release builds and
    regressions. Then update this handoff with exact results, commit and push
    once, and do not inspect hosted CI because Batch 156 is not a monitoring
    boundary.
57. Change 20 is complete. The first full Debug run exposed only the intentional
    addition of ten required matrix rows and fourteen diagnostic codes in two
    frozen baselines; seven composed release gates inherited those failures,
    while every functional test passed. The synchronized audit now freezes
    1,210 execute rows, 4,840 evidence cells, 438 exact paths, 117 runtime
    owners, 1,989 diagnostics, 609 bounded sources, 700 SPDX-owned files, and
    227 authored test/control files with reviewed matrix and evidence digests.
58. The corrected full exact-LLVM 22.1.8 Debug tree required no further build
    work and passes 114/114 in 355.83 seconds. The independent Release tree
    regenerated cleanly, built all 414 steps warning-clean with eight workers,
    and passes 114/114 in 322.72 seconds. No sanitizer was run and hosted CI was
    not inspected, as required at this boundary. Review the final diff/status,
    commit and push this Batch 156 checkpoint exactly once, then begin Batch 157
    from the clean synchronized `codex/v2` branch.

## Batch 155 completed checkpoint - 2026-08-06

1. Start in `/home/colin/projects/fsim`, read this file and the authoritative
   Batch 155 allocation in `implementation_plan_v2.md`, and verify branch
   `codex/v2` remains based on pushed Batch 154 closeout
   `a6704de`. Preserve the dirty accumulated Batch 155 worktree described
   below; do not reset, commit, push, inspect hosted CI, or run a sanitizer
   before Change 20.
2. Batch 155 is expanded into twenty explicit changes. Change 1 is complete in
   the dirty worktree: `DesignUnit` and every SystemVerilog class own
   append-only raw `covergroup` declarations with explicit owner kind, stable
   name and matching end name, exact name/header/body/full spans, and owning
   header/body token copies. Macro-expanded names and closing labels retain
   their expansion stacks and physical/logical source identity independently
   of parser storage.
3. A dedicated `verilog_parser_coverage.cpp` parses declarations in modules,
   interfaces, programs, packages, and classes. Stable cataloged diagnostics
   reject Verilog-2005 use, missing names, missing header semicolons, missing
   `endgroup`, missing or mismatched closing-label names, and duplicates within
   one owner. Balanced header parsing and outer-unit/class terminator recovery
   preserve deterministic declaration boundaries.
4. Validation completed before this handoff: the complete 307-step exact-LLVM
   Debug dependency graph rebuilt warning-clean with eight workers.
   `fsim.frontend`, `fsim.diagnostics-catalog`, and
   `fsim.source-line-budget` pass 3/3, and `git diff --check` is clean.
5. Batch 155 Change 2 is complete in the same dirty worktree. Constructor and
   `with function sample` formals retain direction, `const ref` policy,
   type/name/default tokens, optional owning name tokens, and exact spans.
   Sampling records distinguish event-driven and procedural-profile forms while
   preserving the complete source-owned token stream.
6. Declaration-scope `option` and `type_option` assignments retain explicit
   instance/type scope, owning option-name tokens, value tokens, exact spans,
   and source order. Assignments nested inside future coverpoint/cross bodies
   remain excluded from the covergroup-scope inventory. Stable cataloged
   diagnostics reject unbalanced/empty/duplicate formals, malformed sampling
   events or profiles, and malformed option assignments.
7. Validation after Change 2 rebuilt the complete 307-step exact-LLVM Debug
   dependency graph warning-clean with eight workers. `fsim.frontend`,
   `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
   `git diff --check` is clean.
8. Batch 155 Change 3 is complete in the same dirty worktree. One unified
   source-ordered inventory retains coverpoint and cross kinds, stable explicit
   or deterministic `$coverpoint$N`/`$cross$N` names, owning label tokens,
   exact declaration indices, and complete spans.
9. Coverpoints own expression, `iff` condition, and optional body tokens.
   Crosses own ordered operand spellings/tokens/spans, `iff` conditions, and
   optional bodies. Coverage-body recognition preserves concatenation braces
   in expressions and skips nested bin/option content while scanning later
   declarations. Stable cataloged diagnostics reject missing boundaries,
   expressions, short/empty cross operand lists, malformed guards, and
   duplicate names across the coverpoint/cross namespace.
10. Validation after Change 3 rebuilt the complete 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
    `git diff --check` is clean.
11. Batch 155 Change 4 is complete in the same dirty worktree. A public
    coverage-resolution pass assigns canonical owner/declaration/profile
    identities, resolves coverpoint references and explicit/implicit cross
    operands, binds local or package-qualified covergroup instance types, and
    validates constructor and procedural-sample actuals. Stable
    `FSIM-SV-SEM-208` through `FSIM-SV-SEM-211` cover unknown references,
    invalid cross operands, profile mismatches, and ambiguous types.
12. Each design-unit or class-owned instance retains deterministic
    specialization/runtime identity, constructor actuals, source-ordered
    initial option state, and recursively collected design-unit or class-method
    sample calls. Class-owned declarations also retain deterministic `<object>`
    instance templates. The ordinary source and `.fsimobj` load pipelines both
    run coverage resolution after class resolution.
13. The public owning HIR was split into 1,005-line `design.hpp` and 1,521-line
    `design_core.hpp` after the source gate exposed the former 2,512-line hard
    limit. The complete 370-step exact-LLVM Debug dependency graph then rebuilt
    warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
    `git diff --check` is clean.
14. Batch 155 Change 5 is complete in the same dirty worktree. Coverpoints own
    source-ordered scalar explicit, automatic, default, ignored, and illegal
    bins with stable names/indices, exact signed-decimal values, tokens, and
    spans. Valid array/range syntax remains raw and accepted for Change 6.
15. The public scalar sampler maintains deterministic per-instance bin-hit
    state, creates stable `$auto[value]` identities lazily, excludes ignored
    samples, selects default bins after exact misses, and records both hit state
    and stable `FSIM-SV-COV-001` diagnostics/reports for illegal matches.
    `FSIM-SV-PARSE-319` and `FSIM-SV-SEM-212` reject malformed scalar bins and
    duplicate names.
16. Validation after Change 5 rebuilt the complete 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
    `git diff --check` is clean.
17. Batch 155 Change 6 is complete in the same dirty worktree. Explicit bins
    retain scalar and inclusive-range values, typed/unsized conversion,
    wildcard value/mask/width state, source names, optional declared array
    extents and indices, and deterministic expanded identities. Unsized arrays
    expand one bin per scalar/range value; sized arrays distribute values in
    source order without empty bins. Both paths reject more than 65,536
    bins/values.
18. The public sampler matches exact, inclusive-range, and four-state wildcard
    values through the same stable hit path. `FSIM-SV-PARSE-320` rejects
    malformed or unrepresentable selection/declarator forms and
    `FSIM-SV-SEM-213` rejects invalid or excessive array expansions. Focused
    evidence covers signed typed conversion, range/wildcard matching, sized and
    unsized array identities, grouping, and bounded negative diagnostics.
19. Validation after Change 6 rebuilt the complete 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3,
    `git diff --check` is clean, and the largest changed coverage parser source
    remains 1,167 lines.
20. Batch 155 Change 7 is complete in the same dirty worktree. Transition bins
    own source-ordered scalar/ranged sequences, consecutive, goto, and
    nonconsecutive repetition with fixed/ranged bounds, scalar or ranged
    concatenation delays, sized/unsized array expansion, exact spans, and
    stable expanded identities.
21. The public sampler retains per-instance overlapping prefix state, advances
    repetitions and gaps deterministically, selects the first completed bin in
    source order, and resets selected-bin progress after a hit. Focused evidence
    covers simple sequences, every repetition kind, ranged delay, two arrayed
    sequences, simultaneous overlap, and `FSIM-SV-PARSE-321`/
    `FSIM-SV-SEM-214` rejection.
22. Validation after Change 7 rebuilt the complete 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3,
    `git diff --check` is clean, and changed sources remain below the 2,500-line
    hard limit (`verilog_parser_coverage.cpp` is 1,455 lines).
23. Batch 155 Change 8 is complete in the same dirty worktree. Coverpoint and
    bin `iff` guards execute through deterministic true/false/unknown logic;
    only true admits a sample. The public sampling value carries an exact value,
    unknown mask, and width. Exact/range bins require known values, while
    wildcard bins ignore unknowns only at don't-care positions.
24. The unified selection path gives ignore bins precedence over illegal bins,
    illegal bins precedence over regular bins, and preserves source order
    within each class. Scalar defaults do not steal transition samples;
    default-sequence bins require prior state and no active/matched transition,
    and automatic bins exclude unknown samples. `FSIM-SV-PARSE-322` rejects
    malformed bin guards/default-sequence forms.
25. Validation after Change 8 rebuilt the complete 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3,
    `git diff --check` is clean, and changed sources remain below the 2,500-line
    hard limit.
26. Batch 155 Change 9 is complete in the same dirty worktree. The public
    covergroup transaction validates all requested coverpoint indices before
    mutation, samples them in input/source order, and forms an automatic cross
    tuple only when every resolved operand produced a selected stable bin
    identity.
27. Tuple identities preserve resolved operand order. Repeated tuples increment
    per-instance hit state; ignored or illegal operand bins instead retain a
    stable excluded tuple and exclusion count. Partial inputs form no tuple,
    and duplicate/invalid inputs reject without mutating coverpoint or cross
    state. Focused evidence covers stable repeats, tuple order, exclusion, and
    prevalidation.
28. Validation after Change 9 rebuilt the complete 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3,
    `git diff --check` is clean, and changed sources remain below the 2,500-line
    hard limit.
29. Batch 155 Change 10 is complete in the same dirty worktree. Cross bodies
    own explicit regular/ignored/illegal bins with stable names, declaration
    indices, spans, and selection tokens. `binsof(cp.bin)` and
    `binsof(cp).bin` resolve against cross operands and named scalar/array bins;
    complement, `&&`/`||`, and scalar/ranged `intersect` execute against the
    transaction's selected identities and sampled values.
30. Explicit cross-bin ignore/illegal precedence feeds the same stable tuple
    and exclusion state as automatic products. `FSIM-SV-PARSE-323`,
    `FSIM-SV-SEM-215`, and `FSIM-SV-SEM-216` reject malformed/duplicate bins
    and empty, unknown, or ambiguous selections. Focused evidence covers both
    named qualification spellings, Boolean complement, ranged intersection,
    selection identity, exclusion, and negatives.
31. Validation after Change 10 rebuilt the complete 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3,
    `git diff --check` is clean, and changed sources remain below the 2,500-line
    hard limit.
32. Batch 155 Change 11 is complete in the same dirty worktree. Coverpoint and
    cross `type_option` and instance `option` assignments accept bounded
    integer `weight`, `goal`, and `at_least` values. Instance assignments
    override type assignments independent of source order, and resolved values
    propagate to explicit, expanded, automatic, and lazy cross bins.
33. Weight-zero bins retain their stable selected identity but create no hit
    state. Coverpoint and cross state records own weight, goal, threshold, and
    exact covered status; covered becomes true at the `at_least` hit.
    Accumulation stops at `uint64_t` maximum and emits stable
    `FSIM-SV-COV-002`; `FSIM-SV-SEM-217` rejects invalid option bounds.
    Focused evidence covers precedence, propagation, zero-weight exclusion,
    threshold transitions, and both coverpoint and cross overflow boundaries.
34. Validation after Change 11 rebuilt the complete 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3,
    `git diff --check` is clean, and changed sources remain below the
    2,500-line hard limit (`verilog_parser_coverage.cpp` is 1,619 lines).
35. Batch 155 Change 12 is complete in the same dirty worktree. The public
    `coverage_percentage.hpp` API returns exact raw and goal-normalized
    basis-point percentages for source-ordered coverpoints/crosses, instances,
    and types. Logical regular bins use resolved weights and `at_least`
    covered state; ignored, illegal, and weight-zero bins are excluded.
36. Instance and type goals normalize with deterministic half-up rounding.
    Empty coverage is explicit and remains zero. Type calculation sorts
    instances by runtime identity, retains `per_instance`, and either averages
    instance percentages or merges stable bin/cross hit state before percentage
    calculation according to `merge_instances`. `FSIM-SV-SEM-218` rejects
    out-of-range covergroup goal/weight/Boolean percentage options.
37. Focused evidence covers one-third rounding, instance/type goal
    normalization, zero-weight item exclusion, empty types, deterministic
    instance order, explicit-cross logical-bin percentages, and the difference
    between averaged and merged instance state.
38. Validation after Change 12 rebuilt the complete 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3,
    `git diff --check` is clean, and changed sources remain below the
    2,500-line hard limit (`verilog_parser_coverage.cpp` is 1,659 lines and
    `coverage_percentage.cpp` is 262 lines).
39. Batch 155 Change 13 is complete in the same dirty worktree. The public
    `coverage_execution.hpp` scheduler accepts explicit, event-driven, and
    procedural triggers and routes interpreter, LLVM O0, and LLVM O2 modes
    through one transactional coverage sampler.
40. Stable callback events retain a monotonic sequence, runtime identity,
    trigger/mode, optional bin identity, and sampled value. Callback order is
    pre-sample, coverpoint hits, illegal-bin notification, cross hits, then
    post-sample. A scoped active-sample guard rejects callback reentrancy before
    mutation with `FSIM-SV-COV-003`; `FSIM-SV-COV-004` rejects a trigger
    that does not match the declaration profile.
41. Focused evidence covers all three sampling profiles, ordered hit/illegal
    callbacks, outer completion after a rejected nested sample, and identical
    callback signatures for interpreter and LLVM O0/O2 modes.
42. Validation after Change 13 completed the 82-step exact-LLVM Debug
    dependency build warning-clean with eight workers.
    `fsim.frontend`, `fsim.diagnostics-catalog`, and
    `fsim.source-line-budget` pass 3/3, `git diff --check` is clean, and
    changed sources remain below the 2,500-line hard limit
    (`coverage_execution.cpp` is 142 lines).
43. Batch 155 Change 14 is complete in the same dirty worktree. The public
    `coverage_report.hpp` API builds a structured type/instance/item/bin tree
    with goals, percentages, weights, thresholds, hits, exclusions, illegal
    reports, and exact source spans. Instance-qualified item/bin paths prevent
    ambiguous type-wide queries while retaining stable underlying sampling
    identities.
44. Reports reuse Change 12 type/instance ordering, retain source order for
    coverage declarations and explicit bins, and sort automatic identities and
    realized cross tuples. Public instance/item/bin query helpers return null
    for misses. The text renderer emits deterministic two-decimal percentages
    and the same complete state as the structured tree.
45. Focused evidence proves reversed input instance order renders byte-for-byte
    identically, instance/item/bin queries select exact paths, missing paths
    reject cleanly, source identity survives, and explicit-cross reports expose
    tuple hit/exclusion state, goals, weights, and thresholds.
46. Validation after Change 14 completed the 82-step exact-LLVM Debug
    dependency build warning-clean with eight workers.
    `fsim.frontend`, `fsim.diagnostics-catalog`, and
    `fsim.source-line-budget` pass 3/3, `git diff --check` is clean, and
    changed sources remain below the 2,500-line hard limit
    (`coverage_report.cpp` is 262 lines).
47. Batch 155 Change 15 is complete in the same dirty worktree. The public
    `coverage_observation.hpp` API projects structured coverage reports into
    stable debugger observations for multiple roots. Instance-qualified paths,
    canonical alias targets, and sorted output retain percentage, goal, hit,
    exclusion, threshold, covered, and illegal state without host addresses.
48. Public trace projection adds exact time/delta coordinates and alias paths
    while retaining the shared execution scheduler's callback sequence,
    runtime/bin identities, optional sampled values, and
    pre/hit/illegal/post ordering. Meaningful unsigned and Boolean debugger
    values plus hit trace events are explicitly VCD-compatible.
49. Focused evidence proves stable ordering, multiple roots, aliases, canonical
    paths, no host-address spellings, VCD compatibility, and exact parity with
    the Change 13 execution callback stream.
50. Validation after Change 15 completed the full incremental exact-LLVM Debug
    build warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3,
    `git diff --check` is clean, and changed sources remain below the
    2,500-line hard limit (`coverage_observation.cpp` is 147 lines).
51. Batch 155 Change 16 is complete in the same dirty worktree. The public
    `coverage_persistence.hpp` API owns declarations/specializations,
    instances, option and hit/progress state, reports, callback events, traces,
    aliases, and stable identities at the `BuiltProject` boundary. Derived
    reports can be refreshed after mutable sampling state changes.
52. Portable owning-unit schema 11 preserves complete coverage definitions and
    source identity through `.fsimobj`. Standalone coverage schema 1 is a
    required checksummed `sv-coverage` `.fsimdesign` payload; load restores it
    into the same owning project state and remains valid after design-directory
    relocation.
53. Exact nonempty codec evidence retains bin hits, transition/previous
    progress, cross/exclusion/illegal state, rendered reports, callbacks,
    traces, aliases, and byte-identical deterministic reserialization. Stable
    identities contain no host addresses.
54. The application artifact matrix proves class-owned coverage definitions,
    template instances, reports, and identities survive source compilation,
    `.fsimobj` reload, standalone `.fsimdesign`, mapped-library relocation,
    interpreter execution, and cold/warm LLVM O0/O2 builds.
55. Validation after Change 16 completed the full 135-step exact-LLVM Debug
    build warning-clean with eight workers. `fsim.frontend`,
    `fsim.library.artifact`, `fsim.artifact.object`, `fsim.artifact.design`,
    `fsim.application`, `fsim.diagnostics-catalog`, and
    `fsim.source-line-budget` pass 7/7; `git diff --check` is clean, and the
    largest touched production source is 912 lines.
56. Batch 155 Change 17 is complete in the same dirty worktree. The negative
    matrix now covers malformed declarations/profiles/items/bins/transitions,
    exact name resolution and class-qualified ambiguity, type and option
    bounds, duplicate coverpoint/cross bins, cross selection, trigger mismatch,
    reentrancy, illegal samples, and hit overflow.
57. Stable `FSIM-SV-SEM-219` rejects real, shortreal, realtime, string,
    chandle, event, and void constructor/sample formals outside the bounded
    integral coverage model. Stable `FSIM-SV-SEM-220` rejects unsupported
    coverpoint and cross `with`/`matches` bin selections.
58. Focused assertions require exactly one cataloged diagnostic for the new
    bounded-type and unsupported-form boundaries plus the previously
    unasserted ambiguous type, duplicate cross-bin, and unknown-option paths.
59. Validation after Change 17 completed the full incremental 20-step
    exact-LLVM Debug build warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3,
    `git diff --check` is clean, and changed sources remain below the
    2,500-line hard limit (`verilog_parser_coverage.cpp` is 1,695 lines).
60. Batch 155 Change 18 is complete in the same dirty worktree. Public
    `coverage_limits.hpp` defines exact 4,096-declaration, 65,536-bin,
    1,048,576-product/work, 4,096-input, and 65,536-state-record bounds.
61. Static validation at resolution rejects declaration, aggregate-bin,
    transition-work, and cross-product overflow with stable
    `FSIM-SV-SEM-221`. Sampling/execution preflight worst-case storage growth
    and rejects excessive transaction inputs or persistent state with stable
    `FSIM-SV-COV-005` before callbacks or mutation. Existing
    `FSIM-SV-COV-002` retains exact unsigned hit-count overflow ownership.
62. Focused resource evidence constructs every boundary independently and
    requires one stable diagnostic. A normalized interpreter/LLVM O0/O2
    differential proves identical callback order, structured/text reports,
    multiple debugger roots, aliases, and trace events. The Change 16
    application matrix again proves object/design artifacts, relocation,
    mapped libraries, and cold/warm O0/O2 cache parity.
63. Validation after Change 18 completed the full 82-step exact-LLVM Debug
    dependency build warning-clean with eight workers. `fsim.frontend`,
    `fsim.library.artifact`, `fsim.artifact.object`, `fsim.artifact.design`,
    `fsim.application`, `fsim.diagnostics-catalog`, and
    `fsim.source-line-budget` pass 7/7; `git diff --check` is clean, and the
    largest changed source remains below the 2,500-line hard limit.
64. Preserve all accumulated Batch 155 edits and begin Change 19 by
    synchronizing README, architecture, language support, diagnostics, feature
    rows, legality/release/differential/inventory baselines, release-candidate
    digests, focused gates, and this resumable handoff. Keep builds at eight or
    more workers.
65. Batch 155 Change 19 is complete in the same dirty worktree. Public README,
    architecture, and language-support documentation now distinguish the
    bounded executable covergroup surface from unrestricted temporal coverage,
    coverage-driven randomization, foreign interfaces, and UVM. The feature
    matrix adds `SV-761` through `SV-770` with exact positive, negative,
    implementation, and runtime evidence.
66. The synchronized release inventory freezes 1,200 executable rows, 4,800
    evidence cells, 426 exact paths (188 test, 222 production, 16 release), 114
    runtime owners, 1,975 cataloged diagnostics, 590 bounded sources, 680
    authored artifacts, and 222 authored test/control files. The reviewed
    matrix digest is
    `07862d8c6b20770ce61076ab21072a69d87cebb7632af3f57d8a74616005c9ed`;
    the evidence-path digest is
    `42df8d80fbb0fd947299c7641d2c224c0b53cdc1e4ab0e60f6afc4a41f9f5e33`.
67. The final incremental exact-LLVM Debug build completes 21/21
    warning-clean with eight workers. `fsim.frontend`,
    `fsim.library.artifact`, `fsim.artifact.object`,
    `fsim.artifact.design`, `fsim.application`,
    `fsim.diagnostics-catalog`, `fsim.source-line-budget`, and the legality,
    release, SystemVerilog, differential, inventory, and final-candidate audits
    pass 13/13 in 38.26 seconds. `git diff --check` is clean.
68. Preserve the complete dirty Batch 155 worktree and begin Change 20. Run
    full non-sanitized exact-LLVM Debug and Release builds with at least eight
    workers, then run the complete release gates in both configurations.
    Record the exact results here and in the plan, commit and push the
    accumulated Batch 155 checkpoint once, and do not run a sanitizer or
    inspect hosted CI because Batch 155 is not a monitoring boundary.
69. Batch 155 Change 20 is complete. The first full Debug regression exposed
    one VITAL relocation fixture that copied every prior standalone-state
    payload but omitted the newly required `sv-coverage.bin`; adding that
    payload restores the relocated portable-artifact contract. The first
    optimized build exposed a GCC maybe-uninitialized warning on a
    conditionally constructed automatic-bin optional; explicit emplacement
    retains the same semantics and is warning-clean.
70. The final exact-LLVM 22.1.8 Debug tree builds warning-clean with eight
    workers and passes 114/114 in 366.52 seconds. Release regenerates and
    completes all 377 steps warning-clean with eight workers, then passes
    114/114 in 308.38 seconds. No sanitizer or hosted CI was run or inspected
    because Batch 155 is not a monitoring boundary. Run the final candidate
    and diff checks, commit and push this accumulated Batch 155 checkpoint
    exactly once, then begin Batch 156 from the clean synchronized branch.

## Batch 154 completed checkpoint - 2026-08-06

1. Start in `/home/colin/projects/fsim`, read this file and the authoritative
   Batch 154 allocation in `implementation_plan_v2.md`, and verify branch
   `codex/v2` remains based on pushed Batch 153 closeout
   `a105a02c706901ccde5bd0ca01ae76f3c5b1ee34`. Preserve the dirty accumulated
   Batch 154 worktree described below; do not reset, commit, push, inspect
   hosted CI, or run a sanitizer before Change 20.
2. Batch 154 Change 1 is complete in the dirty worktree. `DesignUnit` owns
   append-only sequence, property, and checker declaration records with exact
   kind, name, name/header/body/full spans, and owning header/body `Token`
   vectors. The tokens preserve expansion stacks and physical/logical source
   identity independently of parser storage so Changes 2-4 can structure
   formals, locals, clocks, disables, and references without reparsing text.
3. A dedicated `verilog_parser_assertions.cpp` parses declaration boundaries
   for modules, interfaces, and programs. Stable cataloged negatives reject
   missing names, header semicolons, or matching terminators; duplicate names,
   mismatched closing labels, and Verilog-2005 use also reject. Focused evidence
   retains a sequence delay range, property clock/disable/reference, and
   checker-owned clocking/assertion tokens with exact source spans.
4. Validation completed before this handoff: `fsim_frontend_tests` and the
   complete 307-step exact-LLVM Debug dependency graph rebuilt warning-clean
   with eight workers. `fsim.frontend`, `fsim.diagnostics-catalog`, and
   `fsim.source-line-budget` pass 3/3, and `git diff --check` is clean.
5. Batch 154 Change 2 is complete in the same dirty worktree. Every declaration
   now owns structured formal arguments and leading local variables without
   discarding the raw owning tokens from Change 1. Formal records distinguish
   value, sequence, property, and untyped arguments and retain direction,
   `local`, type/default tokens, names, and exact spans. Local records retain
   shared type, declarator, initializer, name, and exact spans; top-level comma
   splitting remains delimiter-aware.
6. Stable cataloged negatives reject malformed formal headers, individual
   formals, and local declarations as well as duplicate formal names and
   formal/local name collisions. Focused evidence covers defaults, directions,
   formal kinds, multiple declarators, and local initializers for sequence,
   property, and checker declarations.
7. Validation after Change 2 rebuilt the full 307-step exact-LLVM Debug
   dependency graph warning-clean with eight workers. `fsim.frontend`,
   `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
   `git diff --check` is clean.
8. Batch 154 Change 3 is complete in the same dirty worktree. Optional
   declaration clocks own their event tokens and exact span, optional `disable
   iff` clauses own their condition tokens and exact span, and the remaining
   expression token stream starts after leading locals, clock, and disable
   structure. Original body tokens remain unchanged and source-owned.
9. Both parenthesized and named clock events are represented. Stable cataloged
   negatives reject missing, empty, or unbalanced clock events and `disable
   iff` conditions. Focused evidence covers clocks following local declarations,
   property clock/disable composition, expression remainder ownership, and exact
   source identity.
10. Validation after Change 3 rebuilt the full 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
    `git diff --check` is clean.
11. Batch 154 Change 4 is complete in the same dirty worktree. Reference
    collection runs after the complete design-unit declaration region is known
    and retains canonical path spelling, path components, owning tokens, and an
    exact span for every occurrence. Kinds distinguish formal, local-variable,
    design-unit object, assertion-declaration, hierarchical, and package paths.
12. Forward assertion-declaration references resolve without declaration-order
    leakage. Stable `FSIM-SV-SEM-196` rejects unknown unqualified names;
    hierarchical and package-qualified paths stay explicit for later hierarchy
    and package specialization. Focused evidence covers all six reference kinds
    and the unresolved-name negative.
13. Validation after Change 4 rebuilt the full 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
    `git diff --check` is clean.
14. Batch 154 Change 5 is complete in the same dirty worktree. Sequence
    declarations now own ordered elements and source-spanned concatenation
    delays independently of the raw expression tokens. Scalar and ranged `##`
    forms retain exact minimum/maximum token ownership.
15. Element abbreviations distinguish consecutive `[*]`, nonconsecutive `[=]`,
    and goto `[->]` repetition and retain optional repetition ranges. Stable
    `FSIM-SV-PARSE-298`/`299` negatives reject missing or malformed delays,
    empty concatenation elements, and repetition without an operand. Focused
    evidence covers scalar/ranged delays and all three repetition forms.
16. Validation after Change 5 rebuilt the full 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
    `git diff --check` is clean.
17. Batch 154 Change 6 is complete in the same dirty worktree. Scalar `##0`
    delays carry an explicit fusion annotation while retaining the ordinary
    delay range and source span. Every top-level `intersect` operand owns its
    exact token range and span independently of the original sequence tokens.
18. Stable `FSIM-SV-PARSE-300` rejects empty left or right intersection
    operands. Focused evidence covers fusion/non-fusion classification, chained
    three-operand intersection, nested-parenthesis exclusion, exact source
    identity, and both malformed sides.
19. Validation after Change 6 rebuilt the full 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
    `git diff --check` is clean.
20. Batch 154 Change 7 is complete in the same dirty worktree. Every top-level
    `throughout` and `within` occurrence owns typed left/right operand tokens
    and an exact full span. Each `first_match(...)` occurrence separately owns
    its sequence argument, optional match-item tokens, and exact call span.
21. Stable `FSIM-SV-PARSE-301` rejects missing binary operands and missing,
    empty, or unbalanced `first_match` arguments. Focused evidence covers both
    binary operators in one expression, a nested delay range, match-item
    assignment ownership, exact source identity, and four malformed forms.
22. Validation after Change 7 rebuilt the full 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
    `git diff --check` is clean.
23. Batch 154 Change 8 is complete in the same dirty worktree. `.matched` and
    `.triggered` endpoint records live on every assertion declaration and own
    exact receiver tokens/spelling, invocation actuals, endpoint kind, optional
    empty method parentheses, and complete source span.
24. Unqualified receivers must resolve to sequence declarations or sequence
    formals; hierarchical/package paths remain explicit for later
    specialization. Stable `FSIM-SV-PARSE-302` rejects missing/malformed
    receivers and nonempty endpoint arguments, while `FSIM-SV-SEM-197` rejects
    ordinary-object receivers. Focused evidence covers declarations with
    actuals, sequence formals, deferred hierarchy, both methods, both spelling
    forms, exact spans, and parse/semantic negatives.
25. Validation after Change 8 rebuilt the full 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
    `git diff --check` is clean.
26. Batch 154 Change 9 is complete in the same dirty worktree. Property
    expressions retain typed top-level overlapped `|->` and nonoverlapped
    `|=>` implications with exact source-owned antecedent/consequent tokens and
    full spans. Scalar and ranged `##` property delays reuse the sequence-range
    representation and retain exact spans plus explicit scalar-zero fusion.
27. Recognition accepts the lexer's maximal-munch `|=` plus `>` split for
    nonoverlapped implication. Stable `FSIM-SV-PARSE-303` rejects empty
    implication operands and missing, empty, or unbalanced property delay
    values/ranges. Focused evidence covers both implication kinds, both operand
    streams, scalar fusion, a ranged delay, exact source identity, and four
    malformed forms.
28. Validation after Change 9 rebuilt the full 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
    `git diff --check` is clean.
29. Preserve all accumulated Batch 154 edits and begin Change 10's
    `until`/`s_until` and `nexttime` property ownership. Keep builds at eight
    or more workers.
30. Batch 154 Change 10 is complete in the same dirty worktree. Property
    expressions retain typed `until`, `s_until`, `until_with`, and
    `s_until_with` operations with exact source-owned left/right tokens and full
    spans. Typed `nexttime` and `s_nexttime` records retain optional bracketed
    count tokens, exact operands, strengths, and complete source spans.
31. Stable `FSIM-SV-PARSE-304` rejects empty until operands, missing nexttime
    operands, and empty or unbalanced counts. Focused evidence covers all four
    until variants, both nexttime strengths, counted and uncounted forms, exact
    source identity, and four malformed forms.
32. Validation after Change 10 rebuilt the full 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
    `git diff --check` is clean.
33. Preserve all accumulated Batch 154 edits and begin Change 11's
    `always`/`s_always`, `eventually`/`s_eventually`, and `strong`/`weak`
    property ownership. Keep builds at eight or more workers.
34. Batch 154 Change 11 is complete in the same dirty worktree. Property
    expressions retain typed `always`, `s_always`, `eventually`, and
    `s_eventually` recurrence records with optional exact bracketed ranges,
    source-owned operands, and complete spans. Typed `strong(...)` and
    `weak(...)` wrappers retain exact sequence operands and call spans.
35. Stable `FSIM-SV-PARSE-305` rejects missing recurrence operands, empty or
    unbalanced recurrence ranges, and missing, empty, or unbalanced
    strength-wrapper operands. Focused evidence covers all four recurrence
    kinds, ranged and unbounded forms, both strength wrappers, nested sequence
    delays, exact source identity, and four malformed forms.
36. Validation after Change 11 rebuilt the full 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
    `git diff --check` is clean.
37. Preserve all accumulated Batch 154 edits and begin Change 12's
    accept/reject, abort, and vacuity-policy ownership. Keep builds at eight or
    more workers.
38. Batch 154 Change 12 is complete in the same dirty worktree. Property
    expressions retain typed `accept_on`, `reject_on`, `sync_accept_on`, and
    `sync_reject_on` abort records with independent asynchronous/synchronous
    policy and vacuous-success/failure outcome. Each record owns exact condition
    tokens, property-operand tokens, and a complete source span.
39. Stable `FSIM-SV-PARSE-306` rejects missing, empty, or unbalanced abort
    conditions and missing property operands. Focused evidence covers all four
    operators, both synchronization policies, both outcomes, a compound
    condition, exact source identity, and four malformed forms.
40. Validation after Change 12 rebuilt the full 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
    `git diff --check` is clean.
41. Preserve all accumulated Batch 154 edits and begin Change 13's concurrent
    assert/assume/cover/restrict scheduling ownership. Keep builds at eight or
    more workers.
42. Batch 154 Change 13 is complete in the same dirty worktree. Design units
    retain typed concurrent `assert`, `assume`, `cover`, and `restrict
    property` records with optional labels, exact property tokens, and complete
    source spans.
43. Every directive explicitly owns Preponed sampling, Observed evaluation, and
    Reactive action-region policy. Stable `FSIM-SV-PARSE-307` rejects missing
    `property`, missing/unbalanced/empty property parentheses, and missing
    terminators. Focused evidence covers all four kinds, labeled/unlabeled
    forms, exact source identity, all three regions, and four malformed forms.
44. Validation after Change 13 rebuilt the full 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
    `git diff --check` is clean.
45. Preserve all accumulated Batch 154 edits and begin Change 14's assertion
    pass/fail action and control ownership. Keep builds at eight or more workers.
    Do not reset, commit, push, inspect hosted CI, or run a sanitizer before
    Change 20; Batch 154 is neither monitoring boundary.
46. Batch 154 Change 14 is complete in the same dirty worktree. Concurrent
    directives independently retain pass and failure action presence, exact
    owning token streams, and source spans. Focused evidence covers simple and
    balanced compound pass actions, ordinary and failure-only action blocks,
    exact source identity, and the explicit null pass action before `else`.
47. All ten procedural assertion-control system tasks remain ordinary task-call
    statements while owning typed control policy and the existing parsed
    argument vectors. Stable `FSIM-SV-PARSE-308` rejects missing or unbalanced
    actions, and `FSIM-SV-SEM-198` rejects restrict actions and cover failure
    actions without reclassifying the existing `FSIM-SV-PARSE-307` malformed
    directive cases.
48. Validation after Change 14 rebuilt the full 307-step exact-LLVM Debug
    dependency graph warning-clean with eight workers. `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 3/3, and
    `git diff --check` is clean.
49. Preserve all accumulated Batch 154 edits and begin Change 15's callback,
    debugger, trace, and coverage-count ownership. Keep builds at eight or more
    workers. Do not reset, commit, push, inspect hosted CI, or run a sanitizer
    before Change 20; Batch 154 is neither monitoring boundary.
50. Batch 154 Change 15 is complete in the same dirty worktree. Public
    SystemVerilog semantic HIR now owns one concurrent-assertion descriptor per
    directive with a stable explicit or synthesized name, deterministic
    coverage slot, exact property/pass/failure spelling, source/origin
    provenance, and the Preponed/Observed/Reactive region policy.
51. Observer policy explicitly selects assertion callbacks for assert/assume
    failures while keeping all four directive kinds visible to the debugger,
    trace, and coverage surfaces. Cover and restrict do not manufacture a
    failure callback. Focused HIR evidence covers every kind, both naming forms,
    action/source ownership, observer policy, and stable slot ordering.
52. Validation after Change 15 rebuilt the complete 107-step affected
    exact-LLVM Debug graph warning-clean with eight workers.
    `fsim.application.systemverilog_hir`, `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 4/4, and
    `git diff --check` is clean.
53. Preserve all accumulated Batch 154 edits and begin Change 16's executable
    scheduling, shared callback/debugger/trace delivery, runtime coverage
    counters, and deterministic multiple-root ownership. Keep builds at eight
    or more workers. Do not reset, commit, push, inspect hosted CI, or run a
    sanitizer before Change 20; Batch 154 is neither monitoring boundary.
54. Batch 154 Change 16 is complete in the same dirty worktree. Named or inline
    scalar concurrent properties lower into ordinary source-spanned assertion
    processes with stable explicit/synthesized names, property clock edge
    scheduling, shared assertion debug points, and existing pass/failure report
    delivery. Repeated instances own independent runtime processes.
55. Engine-neutral hidden outcome markers feed deterministic public
    `ConcurrentAssertionCoverage` counts and retained
    `ConcurrentAssertionEvent` trace records. The public assertion callback
    receives the exact retained event, including stable name/process/kind/slot,
    pass/failure/disabled outcome, time/delta, and action-suppression state.
56. Typed `$asserton`, `$assertoff`, `$assertkill`, pass/failure on/off, and
    nonvacuous/vacuous controls execute without changing their ordinary
    task-call HIR. Constant `$assertcontrol` policy selectors share the same
    runtime path. Disabled samples remain trace-visible without incrementing
    coverage; independently disabled pass/failure actions and reports are
    suppressed and restore deterministically.
57. Focused application evidence compares interpreter, LLVM O2, and LLVM
    debug/O0 output, reports, callbacks, events, coverage, and process identity.
    It proves 20 deterministic events across two leaf instances and 40 across
    two aliased roots, including exact control suppression and four independent
    coverage records per root.
58. Validation after Change 16 rebuilt the complete exact-LLVM Debug tree
    warning-clean with eight workers. `fsim.application.assertions`,
    `fsim.application.systemverilog_hir`, `fsim.frontend`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 5/5, and
    `git diff --check` is clean. Preserve the accumulated dirty worktree and
    begin Change 17's formal/type/clock/resource negative matrix. Do not reset,
    commit, push, inspect hosted CI, or run a sanitizer before Change 20.
59. Batch 154 Change 17 is complete in the same dirty worktree. The bounded
    executable slice no longer silently omits unsupported concurrent
    properties: `FSIM-SV-SEM-199` rejects actual/formal/local-variable use,
    `FSIM-SV-SEM-200` rejects unsupported predicate expressions and object
    types, and `FSIM-SV-SEM-201` rejects clocks other than one direct
    design-unit object with an optional edge.
60. The deterministic 256-process per-design-unit executable assertion limit
    and `FSIM-SV-SEM-202` were removed in Batch 165 Change 12. Focused evidence
    now publishes all 257 directives, while the three semantic boundary codes
    and malformed empty-property guard still prevent secondary diagnostics or
    crashes.
61. Validation after Change 17 rebuilt the complete exact-LLVM Debug tree
    warning-clean with eight workers. `fsim.frontend`,
    `fsim.application.assertions`, `fsim.diagnostics-catalog`, and
    `fsim.source-line-budget` pass 4/4, and `git diff --check` is clean.
    Preserve the accumulated dirty worktree and begin Change 18's
    interpreter/LLVM and artifact differential matrix. Do not reset, commit,
    push, inspect hosted CI, or run a sanitizer before Change 20.
62. Batch 154 Change 18 is complete in the same dirty worktree. One shared
    assertion capture path now compares direct interpreter, LLVM O2, LLVM
    debug/O0, and aliased multiple-root execution with compiled-object plus
    standalone-design artifact reload.
63. The artifact path proves interpreter, cold and warm compiled execution,
    native-cache reuse, and relocated `.fsimdesign` execution. Output, stable
    process identity, callback events, retained trace events, and coverage are
    exact. Report message/severity/time/delta and source filename/line/column
    are exact while the portable artifact path remains intentionally normalized.
64. Validation after Change 18 keeps the complete exact-LLVM Debug tree
    warning-clean with eight workers. `fsim.application.assertions`,
    `fsim.library.artifact`, `fsim.artifact.object`, `fsim.artifact.design`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 6/6, and
    `git diff --check` is clean. Preserve the accumulated dirty worktree and
    begin Change 19's documentation, diagnostics, inventories, and resumable
    handoff. Do not reset, commit, push, inspect hosted CI, or run a sanitizer
    before Change 20.
65. Batch 154 Change 19 is complete in the same dirty worktree. Public README,
    architecture, and language-support documentation distinguish complete
    typed sequence/property/checker and concurrent-directive ownership from the
    deliberately bounded executable scalar property slice. The feature matrix
    adds `SV-751` through `SV-760` with exact positive, negative,
    implementation, and runtime evidence.
66. The synchronized release inventory now freezes 1,190 executable rows,
    4,760 evidence cells, 416 exact paths (187 test, 213 production, 16
    release), 113 runtime owners, 1,936 cataloged diagnostics, 571 bounded
    sources, and 661 authored artifacts. The reviewed matrix digest is
    `f4b0eaf84c9f0a953cf835615abb7a02e41e90fe9ee26a4516412ee141137ae4`;
    the evidence-path digest is
    `1510f52287b0e83852c0f0291487f54af96780722ec232790367db41079dbdfd`.
67. The legality, release, SystemVerilog, differential, inventory, and final
    release-candidate audits pass with those exact baselines. Preserve the
    accumulated dirty worktree and begin Change 20's full non-sanitized LLVM
    Debug/Release build and test gates. After both configurations pass, record
    the evidence here and in the plan, then commit and push once. Do not run a
    sanitizer or inspect hosted CI at this batch boundary.
68. Change 19's complete 11-gate focused run exposed a stale semantic-HIR
    fixture rather than permitting it to be masked by the release-only gates.
    Assertion local-type recognition now rejects a would-be qualified type
    prefix ending in `.` or `::`, so a selected expression such as
    `link.valid` remains an expression instead of becoming a false local
    declaration. The checked HIR fixture uses the bounded direct-scalar runtime
    predicate, prints retained diagnostics on failure, and accounts for one
    generated process per concurrent directive. The rebuilt assertion, HIR,
    frontend, catalog, source, legality, release, SystemVerilog, differential,
    inventory, and release-candidate gates pass 11/11; `git diff --check` is
    clean.
69. Batch 154 Change 20 is complete. The complete exact-LLVM 22.1.8 Debug tree
    is warning-clean and passes 114/114 tests in 362.82 seconds. The Release
    tree regenerated and rebuilt all 370 steps warning-clean with eight workers
    and passes 114/114 tests in 304.68 seconds. No sanitizer or hosted CI was
    run or inspected because Batch 154 is neither boundary.
70. Commit and push this accumulated Batch 154 checkpoint exactly once, then
    begin Batch 155 by expanding its locked functional-coverage allocation into
    twenty numbered changes before implementation. Keep every local build at
    eight or more workers, retain full simulation logs, and do not inspect
    hosted CI before Batch 160's monitoring boundary.

## Batch 153 completed checkpoint - 2026-08-06

1. Start in `/home/colin/projects/fsim`, read this file and the authoritative
   Batch 153 allocation in `implementation_plan_v2.md`, and verify branch
   `codex/v2` remains based on pushed Batch 152 closeout
   `ebf527d6f661506ddf25b1988509bf12fb11c415`. Preserve the dirty accumulated
   Batch 153 worktree described below; do not reset, commit, push, inspect
   hosted CI, or run a sanitizer before Change 20.
2. Batch 153 Change 1 is complete in the dirty worktree. Explicit
   `program ... endprogram` declarations use the shared SystemVerilog unit
   parser and retain distinct append-only `SystemVerilogProgram`,
   `systemverilog_program`, and `sv::UnitKind::program` identities across the
   frontend design, general semantic model, and SystemVerilog HIR. Parameters,
   ports, typedefs, functions, tasks, initial/final processes, source spans,
   and matching end labels remain owned by the program. Library/object/compiler
   identity renderers use the stable `program` spelling.
3. Change 2 is complete in the same dirty worktree. SystemVerilog program
   design units participate in qualified and simple top selection,
   logical-library candidate resolution, same-language instance binding,
   specialization, and named-type/package-import preparation. A Verilog top
   request cannot select a program. The focused fixture elaborates a
   parameterized program child with exact port width, imported typedef,
   parameter value, child path, and stable `sv:work.program(driver)`
   specialization identity, then selects a standalone program through both
   qualified and simple root requests. Initialization/final behavior and
   hierarchy/debug closure remain assigned to Change 4.
4. Validation completed before this handoff: `fsim_elaboration_tests` built
   with eight workers and `fsim.elaboration` passes after the final
   wrong-language negative. The complete exact-LLVM Debug tree rebuilt its 22
   affected steps with eight workers. The SystemVerilog-HIR, diagnostic
   catalog, 2,500-line source-policy, and elaboration gates all pass, and
   `git diff --check` is clean.
5. Change 3 is complete in the same dirty worktree. `SchedulerPhase::reactive`
   is ordered after update and before postponed work. Program-owned SimIR and
   DesignIR processes carry explicit reactive ownership, and initial, timed,
   event, sensitivity, delta, fork, and zero-delay resumes preserve that
   region. Ordinary module processes remain active/inactive. The C API appends
   `FSIM_SCHEDULER_PHASE_REACTIVE = 5` while retaining update 3 and postponed
   4. The end-to-end fixture proves a module NBA commits before the program
   child samples it and exposes exactly one reactive runtime process.
6. Validation completed before this handoff: the focused runtime and
   elaboration targets rebuilt with eight workers and pass 2/2. The first full
   exact-LLVM Debug build found the exhaustive public phase converter; after
   appending the C value and converter case, the complete remaining 182-step
   build succeeds with eight workers. Runtime, elaboration, public API, C
   header, SystemVerilog-HIR, diagnostic-catalog, and 2,500-line source-policy
   gates pass, and `git diff --check` is clean.
7. Change 4 is complete in the same dirty worktree. Program initial and final
   processes retain reactive ownership while module lifecycle processes remain
   active. Both final blocks run exactly once after ordinary completion in
   deterministic module/program phase order. Output callbacks resolve to the
   stable `program_host.active_driver.*` process hierarchy, and statement
   execution points retain the physical `program-instances.sv` source plus the
   program child scope. The final observed NBA-sampled value remains exact.
8. Validation completed before this handoff: `fsim_elaboration_tests` rebuilt
   with eight workers after both lifecycle and debugger additions, and
   `fsim.elaboration` passes. The prior complete exact-LLVM Debug build remains
   current for all implementation files; the final focused test binary is
   rebuilt and green.
9. Change 5 is complete in the same dirty worktree. The frontend design owns
   append-only named clocking blocks on modules, interfaces, and programs.
   Each block retains its event, input/output/inout signal declarations,
   optional alias expressions, source spans, and matching end label.
   Cataloged negatives reject missing directions, duplicate members and
   blocks, undeclared unaliased signals, and mismatched end labels.
10. Validation completed before this handoff: `fsim_frontend_tests` and the
    complete exact-LLVM Debug tree rebuilt warning-clean with eight workers.
    `fsim.frontend`, `fsim.diagnostics-catalog`, and
    `fsim.source-line-budget` pass 3/3, and `git diff --check` is clean.
11. Change 6 is complete in the same dirty worktree. Clocking blocks retain
    default and per-signal input/output skews, posedge/negedge qualifiers,
    exact `#1step`, and time-qualified delays. Procedural `##` controls are
    conservative wait nodes with an explicit literal or expression-valued
    cycle count, kept distinct from project-time delays. Cataloged negatives
    cover incomplete and duplicate default skews, illegal inout skews, and
    cycle delays outside SystemVerilog-2017.
12. Validation completed before this handoff: `fsim_frontend_tests` and all
    307 affected full-tree steps rebuilt warning-clean with eight workers.
    `fsim.frontend`, `fsim.diagnostics-catalog`, and
    `fsim.source-line-budget` pass 3/3, and `git diff --check` is clean.
13. Change 7 is complete in the same dirty worktree. A clocking block name is
    an event wait alias. Input members own hidden sampled storage updated by an
    ordinary active process, while output members resolve to the underlying
    driven signal. The program process remains reactive and therefore observes
    the completed sample before driving its output. Stable hierarchy names
    expose both the sampled member and driven alias. A cataloged elaboration
    negative rejects expression-shaped members outside this bounded signal
    path.
14. Validation completed before this handoff: the focused elaboration target
    and complete exact-LLVM Debug tree rebuilt warning-clean with eight workers.
    `fsim.frontend`, `fsim.application.systemverilog_hir`,
    `fsim.elaboration`, `fsim.runtime`, `fsim.diagnostics-catalog`, and
    `fsim.source-line-budget` pass 6/6, and `git diff --check` is clean.
15. Change 8 is complete in the same dirty worktree. A design unit retains one
    named default clocking block. Procedural `##` waits lower to an executable
    repeated-event loop over that block. Input `#1step` and time skews use
    delayed history signals, output time skews use request-to-signal projected
    drivers, and edge qualifiers select the sampler or driver sensitivity edge.
    The focused program counts two positive edges, changes the source in the
    second edge's slot, samples the one-step-old value, waits for a negative
    output edge, and drives one nanosecond later. Cataloged negatives cover
    repeated, unknown, malformed, and missing default clocking plus unavailable
    one-step precision.
16. Validation completed before this handoff: the complete exact-LLVM Debug
    tree rebuilt 151 downstream steps warning-clean with eight workers.
    `fsim.frontend`, `fsim.application.systemverilog_hir`,
    `fsim.elaboration`, `fsim.runtime`, `fsim.diagnostics-catalog`, and
    `fsim.source-line-budget` pass 6/6, and `git diff --check` is clean.
17. Change 9 is complete in the same dirty worktree. Design-unit variables and
    class properties retain nullable virtual-interface types with optional
    `interface` syntax, parameter actuals, modport restrictions, and
    initializers. Interface design-unit names bypass typedef and class-handle
    resolution. Elaboration checks the interface definition and modport,
    allocates 64-bit nullable storage, and resolves a direct instance
    initializer after child interface elaboration to a deterministic nonzero
    pointer-free identity. Positive coverage observes both concrete and null
    values through interpreter state; stable negatives cover missing types,
    modports, instances, wrong interface types, expression initializers, and
    duplicate declarations.
18. Validation completed before this handoff: the focused elaboration target
    and complete 155-step exact-LLVM Debug tree rebuilt warning-clean with
    eight workers. `fsim.frontend`,
    `fsim.application.systemverilog_hir`, `fsim.elaboration`, `fsim.runtime`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 6/6, and
    `git diff --check` is clean.
19. Change 10 is complete in the same dirty worktree. Existing bounded
    instance-array expansion now has explicit interface coverage: declared
    element order produces distinct indexed hierarchy and member-signal paths.
    Static indexed elements bind through interface/modport ports and initialize
    virtual-interface variables with distinct pointer-free identities. Two
    selections of the same element compare equal, a different element compares
    unequal, and out-of-range or runtime-dynamic selectors reject.
20. Validation completed before this handoff: the focused elaboration target
    rebuilt with eight workers, the complete downstream Debug tree relinked
    warning-clean, and `fsim.frontend`,
    `fsim.application.systemverilog_hir`, `fsim.elaboration`, `fsim.runtime`,
    `fsim.diagnostics-catalog`, and `fsim.source-line-budget` pass 6/6.
    `git diff --check` is clean.
21. Change 11 is complete in the same dirty worktree. Interface-class identity
    and `implements` composition are covered with an implementing class that
    owns a modport-restricted virtual-interface property. Generic `interface`
    ports now forward both the concrete interface design-unit identity and its
    pointer-free handle into child virtual-interface initializers. The
    forwarded alias equals the selected source array element in interpreter
    state.
22. Validation completed before this handoff: focused frontend and elaboration
    targets rebuilt with eight workers, the full Debug tree relinked cleanly,
    and all six frontend, SystemVerilog-HIR, elaboration, runtime, catalog, and
    source-policy gates pass. `git diff --check` is clean.
23. Change 12 is complete in the same dirty worktree. Existing modport
    function/task materialization now has direct virtual-interface fixture
    coverage, and a modport clocking member retains an append-only frontend and
    SystemVerilog-HIR kind while forwarding the block event alias plus sampled
    member aliases through a restricted interface port. Generic interface ports
    forward the concrete specialization identity alongside the design-unit and
    pointer-free handle. A parameterized virtual-interface declaration uses the
    ordinary unit specializer for named or positional actuals and requires its
    canonical identity to equal the selected concrete instance; matching
    defaults/actuals preserve handle equality and a width mismatch emits stable
    `FSIM-ELAB-SVIFACE-010`.
24. Validation completed before this handoff: focused frontend/elaboration
    targets rebuilt with eight workers and their three focused gates passed.
    The first full build exposed the exhaustive SystemVerilog-HIR enum
    converter; after appending the semantic clocking-member value and assertion,
    the complete 154-step downstream exact-LLVM Debug rebuild succeeded
    warning-clean with eight workers. Frontend, SystemVerilog-HIR, elaboration,
    runtime, diagnostic-catalog, and source-policy gates pass 6/6, and
    `git diff --check` is clean.
25. Change 13 is complete in the same dirty worktree. A parameterized interface
    carrying an imported function and clocking member crosses two recursively
    instantiated modport-restricted module ports. Both boundaries preserve the
    concrete interface unit, canonical specialization identity, pointer-free
    handle, callable view, clocking event alias, and sampled member alias. The
    root and twice-forwarded virtual variables compare equal. A simultaneously
    elaborated program root owns an eight-bit specialization and a distinct
    nonzero handle, proving builder state remains root-qualified.
26. Validation completed before this handoff: the focused elaboration target
    rebuilt with eight workers and passed after retaining the restricted view
    at both recursive boundaries. The complete incremental Debug tree has no
    remaining work. Frontend, SystemVerilog-HIR, elaboration, runtime,
    diagnostic-catalog, and source-policy gates pass 6/6, and `git diff
    --check` is clean.
27. Change 14 is complete in the same dirty worktree. Semantic checking now
    recognizes virtual-interface initializers as hierarchy-object references
    rather than ordinary chandle assignments; elaboration remains the sole
    validator of the concrete interface instance, type, specialization, and
    modport. The dedicated application differential observes equal nonzero
    direct/twice-forwarded handles and a clocking sample through interpreter and
    cold/warm LLVM execution at O0 and O2.
28. Change 15 is complete in the same dirty worktree. The application
    signal-change hook and VCD capture include the direct and recursive virtual
    handles plus the forwarded read-only clocking sample. Final state and VCD
    remain byte-identical between interpreter and compiled engines. Cold native
    builds miss and warm builds hit at both optimization levels, and source
    edits still invalidate the affected callable/export cache entries.
29. Validation completed before this handoff: the dedicated application target
    rebuilt warning-clean after the semantic and fixture corrections and its
    full differential passes. The complete incremental Debug tree relinked 18
    downstream targets with eight workers. The dedicated differential plus all
    six frontend, SystemVerilog-HIR, elaboration, runtime, catalog, and
    source-policy gates pass 7/7, and `git diff --check` is clean.
30. Change 16 is complete in the same dirty worktree. Portable owning-unit and
    design-state Type archives now serialize, in identical declaration order,
    the virtual-interface marker, concrete interface name, modport, and retained
    parameter actuals. The new frontend and semantic clocking-member enum values
    are bounded during archive validation. Owning-unit schema is 10,
    portable-library schema is 7, runtime state is 17, semantic state is 2,
    DesignIR state is 2, and class state is 7; tests pin each value and reject a
    future owning-unit/class schema.
31. Direct owning-unit round-trip evidence retains named WIDTH actual 4 and the
    exact `unit_if.view` virtual type. Class-state round-trip evidence retains a
    64-bit `class_view_if#(.WIDTH(4)).view` property. The CLI artifact phase
    compiles and reloads a program-owned interface through `.fsimobj`, publishes
    and reloads `.fsimdesign`, observes equal direct/forwarded nonzero handles
    plus clocking sample `1010`, renames the design, and repeats interpreter,
    compiled, trace, and warm-cache execution unchanged.
32. Validation completed before this handoff: direct library codec and the main
    application shard pass after a warning-policy rename and qualified program
    selector correction. The complete exact-LLVM Debug tree rebuilt/relinked 21
    downstream steps with eight workers. All eleven standard, interface,
    application, library, object-artifact, and design-artifact gates pass, and
    `git diff --check` is clean.
33. Change 17 is complete in the same dirty worktree. Every interface hierarchy
    alias now retains its selected modport view. A restricted actual can cross
    recursive boundaries only through the same restricted view; binding it to
    a generic interface port or a different modport emits stable
    `FSIM-ELAB-SVIFACE-011` immediately. The portable owning-unit round trip
    also retains the append-only clocking modport-member kind and rejects a
    corrupt kind value of 255 before publication.
34. Validation completed before this handoff: the focused elaboration and
    direct library targets rebuilt with eight workers, the complete incremental
    exact-LLVM Debug tree relinked warning-clean, and all eleven frontend,
    SystemVerilog-HIR, library/artifact, diagnostics, source-policy,
    elaboration, application, interface-differential, and runtime gates pass.
    `git diff --check` is clean.
35. Preserve all accumulated Batch 153 implementation, test, plan, diagnostic,
    and handoff edits. Change 18 publishes Batch 153 language-support and
    architecture sections and feature rows `SV-741` through `SV-750`, covering
    program/reactive scheduling, clocking HIR and execution, cycle controls,
    virtual-interface identity and recursive forwarding, restricted views,
    full engine/VCD/cache differentials, and portable artifacts. The current
    deferred matrix no longer lists program or clocking blocks; the historical
    v1 target note remains explicit.
36. Change 19 advances every affected release audit together. The reviewed
    matrix contains 1,180 execute rows and 4,720 evidence cells across 414 exact
    paths, with 1,905 production diagnostics, 570 bounded sources, and 113
    runtime owners. The matrix digest is
    `28623705ee34643f345c98f226b513af67d217adf84618b7c5d89a6160c18cb2`;
    the unchanged sorted evidence-path digest is
    `c76f1dbfecd0fc5392f2f7108da2f2d5c06830cd2777952d833821241d8e1fa5`.
37. Validation completed before this handoff: catalog, source-line, release
    audit, SystemVerilog release, differential release, public release,
    inventory release, installed-public contract, and release-candidate gates
    pass 9/9. `git diff --check` is clean.
38. Change 20 is complete. The exact-LLVM Debug tree is current and its full
    suite passes 114/114 in 144.53 seconds. The exact-LLVM Release tree rebuilt
    all 457 affected steps warning-clean with eight workers and its full suite
    passes 114/114 in 116.53 seconds. Both runs include all release,
    differential, inventory, public, portability, artifact, API/ABI, interface,
    and runtime gates.
39. No sanitizer or hosted CI monitoring ran because Batch 153 is neither
    boundary. The next local sanitizer remains Batch 160 Change 20 under the
    ten-batch cadence, and hosted CI continues to exclude sanitizer jobs.
40. Land this accumulated work as the single Batch 153 closeout commit and push
    `codex/v2`. On restart, verify the branch and `origin/codex/v2` match this
    completed checkpoint, preserve a clean synchronized worktree, and begin
    Batch 154 Change 1's concurrent-assertion declaration ownership.

## Batch 152 completed checkpoint - 2026-08-06

1. Start in `/home/colin/projects/fsim`, read this file and the locked Batch
   152 allocation in `implementation_plan_v2.md`, and verify `codex/v2` remains
   based on pushed Batch 151 closeout
   `91cab87c02f0560401471c7b7495a121d2b29ba3`. Batch 152 Changes 1-20 are
   complete in the single accumulated closeout commit containing this handoff;
   verify the branch tip and origin match before continuing Batch 153 Change 1.
2. Batch 150 is complete. Final implementation repair `0005462` closes the
   Windows class-artifact input stream before renaming its source. Replacement
   hosted run `31054730031` passes all eleven jobs, including the Windows MSVC
   and clang-cl Debug/Release test suites.
3. Batch 151 Change 1 is complete: SystemVerilog constants now own the shared
   arbitrary-width packed representation, retain exact domain/type identity,
   and reject materialization above 16,777,216 bits before resize/allocation.
   Change 2 is also complete: portable arbitrary-width decimal conversion,
   binary/octal/hex materialization, concatenation/replication, and separate
   width/work limits pass exact 128-bit positives and a bounded 10,000,000-bit
   work rejection. Change 3 is complete as well: wildcard/qualified package
   imports, wide specparams, distinct specializations, generate cases, checked
   range projection, and a lossless SystemVerilog-to-VHDL generic boundary
   retain the full packed value. Change 4 is complete: exact 128-bit two-/four-
   state and 96-bit Z profiles cross elaborated signals, SimIR, runtime, and
   interpreter admission while runtime/LLVM validation remains green. Change 5
   is complete: exact resource-bounded 128-bit arithmetic, unary operations,
   signed divide/modulo and negative-power rules, checked wrap/truncation,
   unknown propagation, signed-overflow rejection, and a 9,000-bit work-limit
   rejection pass eleven focused gates. Change 6 is complete: exact wide
   logical/bitwise, ordinary/case/wildcard equality, signed/unsigned comparison,
   saturating shifts, unknown counts, and Logic4/Logic9 sign fill pass eleven
   focused gates. Change 7 is complete: the policy-triggered service split
   reduced the evaluator from 2,493 to 1,984 lines before new work (2,135 after
   completion), and exact wide streaming, reductions, selectors, out-of-range
   Xs, selected constant-function writes, conditional merges, and positional/
   default flat patterns pass eleven focused gates. Change 8 is complete:
   governed builtin and named wide casts, exact ascending/descending packed
   queries, arbitrary-width `$clog2`, deliberate unknown-state conversion,
   128-bit interpreter casting, and a 58-output interpreter/compiled O0/O2
   differential pass eleven focused gates. Change 9 is complete: recursive
   parsing and exact layout now cover 137-bit nested anonymous packed structs,
   recursive Logic4-X/Bit2-zero defaults, nested inner/outer `default:`
   assignment patterns, dotted selectors, and selected updates through the
   interpreter and cold/warm LLVM O0/O2. Member initializers remain assigned to
   Change 11, and anonymous unpacked structs remain assigned to Batch 152.
   Thirteen focused gates pass. Change 10 is complete: unequal-width packed
   unions use maximum-width low-bit payloads and canonical zero padding, tagged
   unions add compact ordinal discriminators, and defaults, constructors,
   patterns, selected reads/writes, comparisons, casts, semantic HIR, and
   invalid timing/context cases pass a 23-output interpreter and cold/warm LLVM
   O0/O2 differential plus thirteen focused gates. The new aggregate-kind
   enumerator is append-only; artifact schema work remains assigned to Change
   16. Change 11 is complete: anonymous/default-base enums, explicit integral
   bases/ranges, recursive member initializers, contextual nested aggregate
   constants, semantic-HIR initializer identity, and aggregate type/object
   query forms pass a 32-output interpreter and cold/warm LLVM O0/O2
   differential plus thirteen focused gates. Invalid nested defaults diagnose
   through `FSIM-ELAB-SVAGG-007`. Artifact retention for the appended frontend
   metadata remains assigned to Change 16. Change 12 is complete: canonical
   packed struct/union/enum identity now governs assignments, initialization,
   parameters, nested patterns, function/task flows and copy-out, equality,
   explicit casts, and same-language ports. Enum literal sizing preserves raw
   range validation and executable nominal identity; integer/logic substitution
   and static-array element selection retain the identity needed by downstream
   checks. A focused acceptance/rejection matrix plus sixteen gates pass.
   Change 13 is complete: exact packed environments now cross prepared and
   ordinary roots plus recursive hierarchy specialization, >64-bit
   cross-language width/signedness adapters use the existing arbitrary-width
   packed operations, and focused 137-bit nominal aggregate, multiple-root,
   interface/modport, net/variable, and 129/137-bit mixed-boundary proofs pass.
   Change 14 is complete: the remaining callable/class admission caps are
   removed, and exact 137-bit constructor/default actuals, module and class
   functions/tasks, automatic/static locals, bounded method recursion,
   ref/inout/output copy-out, suspended continuations, processes, and object
   properties pass full application and focused runtime gates. The existing
   native cache contract uses a narrow cache-only top while complete wide LLVM
   compilation remains assigned to Change 18. Change 15 is complete: direct
   arbitrary-width `$fread`, wide packed memory elements, debugger show/
   deposit/force/release, snapshots, callbacks, VCD/class traces, read/write
   memory text, and public C API services retain exact 137-bit values through
   fourteen green gates. Change 16 is complete: synchronized owning-unit,
   portable-library, runtime, class, and constraint-HIR schemas preserve
   arbitrary-width enum values, tagged/member-initialized profiles, exact
   137-bit artifact values, and class widths through `.fsimobj`, standalone
   `.fsimdesign`, mapped/relocated `.fsimlib`, O0/O2 interpreter/compiled
   service boundaries, and native cold/warm/edit caches. Twelve focused gates
   pass. Change 17 is complete: cataloged width/work/storage, malformed-type,
   unsupported-wide-scan, lossy-boundary, stale-schema, and corrupt-artifact
   negatives reject transactionally. Oversized line input no longer clears
   its destination, malformed retained enums reject in portable/design-state
   writers, and failed object/design/library overwrites preserve existing
   metadata and payload bytes. Change 18 is complete: LLVM file-operation
   validation admits governed arbitrary-width `$fread`, and the exact 137-bit
   packed signal/memory fixture is a fully compiled one-process O0/O2 cold/warm
   cache differential while its interpreter run retains debugger, callback,
   snapshot, and VCD evidence. Fifteen LLVM, application, callable/scheduling,
   aggregate, mixed-boundary, artifact/library, catalog, and source gates pass.
   Change 19 is complete: public architecture/language/README/mixed-boundary
   contracts now distinguish arbitrary-width owning services from the native
   word fast path, four executable feature rows cover Batch 151, the generated-
   constant catalog wording is current, and UVM plus other later surfaces keep
   their locked ownership. The reviewed matrix digest is
   `97da9b1e1135bfea5bb7c2879dce2e1196e448d5ffd5bfd05daff25e65aa4021`;
   inventories are 1,877 diagnostics, 560 bounded sources, and 650 SPDX-owned
   artifacts. All 26 documentation/release/portability contracts pass. Change
   20 is complete: exact LLVM 22.1.8 Debug passes 112/112 in 349.27 seconds;
   exact LLVM 22.1.8 Release rebuilds all 383 steps with eight workers and
   passes 112/112 in 307.98 seconds. No sanitizer ran and no hosted CI was
   inspected because Batch 151 is not a monitoring boundary. Begin Batch 152
   Change 1 from the locked allocation in `implementation_plan_v2.md`, using at
   least eight workers for builds.
4. Batch 151 is not a sanitizer or hosted-CI boundary. Run the local sanitizer
   again at Batch 160 Change 20 under the ten-batch cadence; do not inspect
   documentation-only hosted runs.
5. Batch 152 Change 1 is complete in the uncommitted Batch 152 worktree.
   Leading constant/runtime index prefixes of multidimensional static unpacked
   arrays now produce isolated remaining-rank snapshots and support exact whole
   assignment, partial-target replacement, and fixed-array callable arguments
   for packed/scalar leaves. The exact-LLVM Debug build uses eight workers; core
   elaboration, container elaboration, and the O0/O2 cold/warm aggregate
   application differential pass 3/3. The diagnostic catalog and 2,500-line
   source-policy gates pass 2/2, and `git diff --check` is clean. Preserve this
   dirty accumulated Batch 152 worktree and begin Change 2's multidimensional
   range/indexed slices; do not commit, push, inspect hosted CI, or run a
   sanitizer before Change 20.
6. Change 2 is complete in the same accumulated worktree. Multidimensional
   range/indexed slices now retain remaining rank and trailing dimensions,
   adapt equal per-dimension shapes, snapshot overlapping sources, replace
   nested targets, and cross fixed-array return/task copy-out plus slice query
   paths. Interpreter and compiled O0/O2 cold/warm evidence passes with the
   focused 3/3 application/elaboration matrix; catalog and source-policy gates
   pass 2/2.
7. Change 3 is complete in the same accumulated worktree. Named, anonymous,
   and nested unpacked structs and unions now retain recursive container
   profiles, defaults, member selection/update, snapshot whole-element copies,
   scalar union-arm aliasing, debugger visibility, and deterministic compiled
   cache identity. Missing aggregate braces recover at the declaration boundary
   after an OOM regression exposed by the monolithic frontend suite. The full
   exact-LLVM Debug build succeeds with eight workers; the semantic HIR,
   frontend, elaboration, container elaboration, runtime, LLVM, 40-signal
   interpreter/O0/O2 cold/warm application differential, diagnostic catalog,
   and 2,500-line source-policy matrix passes 9/9. `git diff --check` is clean.
   Continue with Change 4's recursive unpacked patterns, queries, equality, and
   value-copy closure. Preserve the dirty accumulated Batch 152 worktree; do
   not commit, push, inspect hosted CI, or run a sanitizer before Change 20.
8. Change 4 is complete in the same accumulated worktree. Recursive unpacked
   struct/union assignment patterns now cover fixed, dynamic, queue, and
   associative containers with positional, keyed, defaulted, and cataloged
   invalid forms. Recursive type/object queries, corrected aggregate
   dimensions, four-state equality, snapshot copies, associative mutation and
   deletion, union aliasing, debugger views, and deterministic native keys run
   through both execution paths. The explicit aggregate-value profile removes
   the dynamic-array/value-box ambiguity and is validated and retained by
   runtime-state schema 11. The full 363-step exact-LLVM Debug build succeeds
   with eight workers. The 48-signal interpreter/O0/O2 cold/warm application
   differential, full application, container elaboration, runtime, LLVM,
   diagnostic catalog, and 2,500-line source-policy matrix passes 7/7;
   `git diff --check` is clean. Continue with Change 5's string-element
   static/dynamic/queue/nested container execution. Preserve the dirty
   accumulated Batch 152 worktree; do not commit, push, inspect hosted CI, or
   run a sanitizer before Change 20.
9. Change 5 is complete in the same accumulated worktree. Append-only
   string-element and typed nested-element SimIR operations execute fixed,
   dynamic, bounded queue, integral associative, multidimensional static, and
   fixed-to-dynamic nested string containers with exact empty defaults,
   prefix-preserving resize, selected reads/writes, whole copy/comparison, and
   dynamic-array callable copy-in/out. Interpreter and compiled callbacks,
   validation, native-cache identity, debugger views, and runtime-state schema
   12 retain the new operations and recursively owned values. The full
   363-step exact-LLVM Debug build succeeds with eight workers. Full
   application, the interpreter/O0/O2 cold/warm aggregate differential,
   container elaboration, runtime, LLVM, diagnostic catalog, and 2,500-line
   source-policy gates pass 7/7; touched implementation files remain below the
   2,500-line hard limit and `git diff --check` is clean. Continue with Change
   6's canonical string associative indices, traversal, ordering, mutation,
   and resource-governed identity. Preserve the dirty accumulated Batch 152
   worktree; do not commit, push, inspect hosted CI, or run a sanitizer before
   Change 20.
10. Change 6 is complete in the same accumulated worktree. Canonical
    string-indexed associative arrays own a distinct lexicographically ordered
    strict-UTF-8 key store through elaboration, SimIR, interpreter and compiled
    callbacks, portable artifacts, runtime-state schema 13, native-cache
    identity, and debugger rendering. Packed and string elements support
    default lookup, insertion, overwrite, existence, first/last/next/previous
    traversal with string-iterator mutation, deletion, copy, and equality;
    bounded key length, malformed UTF-8, element count, and recursively owned
    storage reject before publication. The focused aggregate differential
    passes through interpreter plus cold/warm LLVM O0/O2, and direct runtime
    evidence covers ordered traversal, selection, deletion, invalid UTF-8,
    oversized keys, and distinct packed/string storage. Crossing the 2,500-line
    hard limit triggered the required refactor: `simir_containers.cpp` is now
    1,777 lines and `simir_container_algorithms.cpp` is 809. The eight-worker
    exact-LLVM Debug build succeeds; full application plus seven focused
    frontend, container-elaboration, runtime, LLVM, application, catalog, and
    source-policy gates pass 8/8, and `git diff --check` is clean. Continue
    with Change 7's explicit ownership and shape-checked SystemVerilog/VHDL and
    standard-descriptor boundaries. Preserve the dirty accumulated Batch 152
    worktree; do not commit, push, inspect hosted CI, or run a sanitizer before
    Change 20.
11. Change 7 is complete in the same accumulated worktree. Fixed recursively
    packable SystemVerilog container ports now cross VHDL and packed-signal
    descriptor boundaries through one central runtime alias service used by
    interpreter and compiled callbacks. Port direction gives each alias
    explicit readable/writable ownership; registration rejects invalid IDs,
    duplicates, slice aliases, unsupported recursive profiles, and width
    mismatches. Elaboration compares fixed dimension counts, aggregate member
    grouping, leaf widths, and two-/four-state domains, so equal flattened
    widths with different recursive shapes reject through
    `FSIM-ELAB-SVPORT-004`. Recursive pack/unpack retains declaration order
    and exact Logic4/Logic9 states. The positive fixture drives a VHDL array of
    four byte vectors through an SV static-array input and returns the reversed
    values through an SV output; its same-width two-word negative rejects.
    The full eight-worker exact-LLVM Debug build succeeds. Frontend, diagnostic
    catalog, 2,500-line source policy, full/container elaboration, runtime,
    LLVM, aggregate application differential, and full application gates pass
    9/9; `git diff --check` is clean. Touched implementation and test files
    remain below 2,000 lines. Continue with Change 8's multichannel and
    recursive memory-file transactional I/O. Preserve the dirty accumulated
    Batch 152 worktree; do not commit, push, inspect hosted CI, or run a
    sanitizer before Change 20.
12. Change 8 is complete in the same accumulated worktree. One-argument
    `$fopen` returns multichannel descriptor bits disjoint from ordinary tagged
    descriptors; combined writes, flushes, and closes validate selected
    channels and retain process ownership. `$readmem*` and `$writemem*` now
    cover deterministic row-major multidimensional storage, quoted escaped
    UTF-8 string elements, and recursively packed aggregate elements. Failed
    text parsing/range validation leaves the complete target unchanged.
    `$fread` likewise accepts multidimensional and recursively packed aggregate
    memories through staged publication. The eight-worker exact-LLVM Debug
    build succeeds. Frontend, diagnostics catalog, 2,500-line source policy,
    full/container elaboration, LLVM, runtime, and the interpreter plus
    cold/warm O0/O2 file-application differential pass 8/8; `git diff --check`
    is clean. Continue with Change 9's runtime real-valued and variable delays.
    Preserve the dirty accumulated Batch 152 worktree; do not commit, push,
    inspect hosted CI, or run a sanitizer before Change 20.
13. Change 9 is complete in the same accumulated worktree. Runtime-valued
    SystemVerilog delay controls now lower packed integral, `time`, `real`,
    `shortreal`, and `realtime` expressions into typed `WaitFor` source
    metadata. Project normalization supplies the timeunit tick scale and
    timeprecision quantum; the shared kernel boundary performs deterministic
    real rounding, unknown/negative/nonfinite/type/overflow rejection, and
    final scheduler admission for interpreter and native frames. Native cache
    schema v85 and runtime-state schema 14 cover the new fields. The full
    eight-worker exact-LLVM Debug build succeeds. Design-artifact, diagnostics
    catalog, 2,500-line source policy, full/container elaboration, LLVM,
    runtime, and interpreter plus cold/warm O0/O2 time-application gates pass
    8/8; `git diff --check` is clean. Continue with Change 10's generalized
    edge expressions and runtime-selected force/release targets. Preserve the
    dirty accumulated Batch 152 worktree; do not commit, push, inspect hosted
    CI, or run a sanitizer before Change 20.
14. Change 10 is complete in the same accumulated worktree. Edge-qualified
    SystemVerilog expressions now retain deduplicated signal dependencies,
    exact four-state transition predicates, baseline refresh, spurious-wake
    re-arming, and mixed direct/expression event-list behavior. Runtime-selected
    force/release bit targets reuse the signed 32-bit dynamic-index mapping in
    both engines; masked underlying writes remain live and release reveals the
    current underlying value. Optional selection metadata advances the native
    cache to schema v86 and runtime state to schema 15. The full eight-worker
    exact-LLVM Debug build succeeds. Frontend, design-artifact, diagnostics
    catalog, 2,500-line source policy, elaboration, LLVM, and the interpreter
    plus cold/warm O0/O2 procedural-assignment differential pass 7/7;
    `git diff --check` is clean. Continue with Change 11's nonlocal and
    suspending references across activation, re-entry, copy-out, and failure
    unwinding. Preserve the dirty accumulated Batch 152 worktree; do not
    commit, push, inspect hosted CI, or run a sanitizer before Change 20.
15. Change 11 is complete in the same accumulated worktree. Automatic function
    and task reference actuals now accept writable nonlocal, indexed, and
    sliced targets. Dynamic selectors are captured once into activation-local
    temporaries before callable execution, so suspension and sequential re-entry
    cannot redirect copy-out; failed calls unwind before publication and leave
    the original actual unchanged. The interpreter plus cold/warm LLVM O0/O2
    application matrix covers module-scope function refs, sequential suspending
    task refs, a selector changed during packed-part activation, and a runtime
    delay exception. The full eight-worker exact-LLVM Debug build succeeds.
    Frontend, diagnostics catalog, 2,500-line source policy, elaboration, LLVM,
    callable-closure, and suspending-task gates pass 7/7; touched callable and
    fixture files remain below 2,000 lines and `git diff --check` is clean.
    Continue with Change 12's nested and nonintegral static locals/tasks,
    specialization, initialization, debugger identity, and restart-safe
    lifetime. Preserve the dirty accumulated Batch 152 worktree; do not commit,
    push, inspect hosted CI, or run a sanitizer before Change 20.
16. Change 12 is complete in the same accumulated worktree. Static callable
    allocation now recursively initializes nested packed, string, and container
    declarations once, records their source identities, and rebinds them at
    lexical block entry without replaying initialization. Fixed-container
    initializer patterns use the typed pattern path, and static tasks may
    suspend sequentially; simultaneous re-entry remains assigned to Change 13.
    Interpreter plus cold/warm LLVM O0/O2 evidence covers two nested function
    calls, two suspended task calls, retained packed/string/fixed-array values,
    exact nested debugger identities and values, and fresh-simulation
    initialization. The full eight-worker exact-LLVM Debug build succeeds.
    Frontend, catalog, 2,500-line source policy, full/container elaboration,
    runtime, LLVM, callable-closure, suspending-task, mutable-string, and
    container-application gates pass 11/11; `git diff --check` is clean.
    Continue with Change 13's simultaneous fork-site re-entry and
    generation-safe process handles. Preserve the dirty accumulated Batch 152
    worktree; do not commit, push, inspect hosted CI, or run a sanitizer before
    Change 20.
17. Change 13 is complete in the same accumulated worktree. Repeated execution
    of one lexical fork site now retains simultaneous live child generations,
    and checked 64-bit process handles combine a monotonic nonzero generation
    with the dense process ID so null, malformed, forged, and stale handles
    reject. Append-only SimIR operations and the SystemVerilog `process` source
    type implement `process::self()`, `status()`, `completed()`, `await()`, and
    `kill()` through interpreter and compiled host boundaries. Completion wakes
    explicit awaiters; recursive kill records `KILLED`; JIT suspension and halt
    paths retain `WAITING` and `FINISHED` consistently. Direct runtime evidence
    covers stale-generation rejection and lifecycle transitions. The fork
    application re-enters the same site twice and checks waiting, finished,
    killed, and completed results through interpreter plus cold/warm LLVM O0/O2.
    The full eight-worker exact-LLVM Debug build succeeds. Frontend, diagnostic
    catalog, 2,500-line source policy, elaboration, runtime, LLVM, and fork-
    application gates pass 7/7; `git diff --check` is clean. Continue with
    Change 14's typed mailboxes and counting semaphores. Preserve the dirty
    accumulated Batch 152 worktree; do not commit, push, inspect hosted CI, or
    run a sanitizer before Change 20.
18. Change 14 is complete in the same accumulated worktree. Contextual typed
    `mailbox #(T)` and `semaphore` source handles lower to append-only SimIR
    create, query, blocking, and nonblocking operations. The runtime owns
    bounded mailbox storage plus FIFO reader/writer queues and FIFO semaphore
    waiters without bypass; wakeups retain blocked continuations and typed
    mailbox reads publish through ordinary copy-out. Direct runtime regressions
    cover capacity, nonblocking results, reader/writer ordering, counted-key
    ordering, and wakeup. The source application covers `num`, put/get/peek,
    try variants, and counted semaphore operations through interpreter plus
    cold/warm LLVM O0/O2. The full eight-worker exact-LLVM Debug build succeeds.
    Frontend, diagnostic catalog, 2,500-line source policy, elaboration,
    runtime, LLVM, and synchronization-application gates pass 7/7;
    `git diff --check` is clean. Continue with Change 15's named-event and
    container ordering interactions, deterministic shuffle, waiter ordering,
    and cross-process visibility. Preserve the dirty accumulated Batch 152
    worktree; do not commit, push, inspect hosted CI, or run a sanitizer before
    Change 20.
19. Change 15 is complete in the same accumulated worktree. SystemVerilog
    `shuffle()` lowers through the shared container-ordering path, and both
    interpreter and compiled execution consume one deterministic per-process
    random stream through an unbiased Fisher-Yates implementation. Named-event
    waiters retain stable process-ID order, while shared queue pushes, shuffle,
    reverse, and observations remain visible between awakened processes. Direct
    runtime evidence proves exact draw consumption and permutation. The source
    application passes interpreter plus cold/warm LLVM O0/O2 with identical
    results and one-specialization native-cache reuse. The full eight-worker
    exact-LLVM Debug build succeeds. Frontend, diagnostic catalog, 2,500-line
    source policy, elaboration, LLVM, named-event, container-application,
    ordering-application, and runtime gates pass 9/9; `git diff --check` is
    clean. Continue with Change 16's scheduler ownership, lifetime,
    cancellation, exception propagation, and failure containment. Preserve the
    dirty accumulated Batch 152 worktree; do not commit, push, inspect hosted
    CI, or run a sanitizer before Change 20.
20. Change 16 is complete in the same accumulated worktree. Cancelable
    scheduler handles now retain owner identity and are live only while their
    work remains pending. Cross-owner and moved-from cancellation cannot affect
    transferred work; execution, explicit cancellation, discard, and owner
    destruction invalidate handles. A throwing callback invalidates its handle,
    releases scheduler run state, propagates once, and preserves later stable-
    order callbacks for a resumed run. The full eight-worker exact-LLVM Debug
    build succeeds. Direct runtime ownership/lifetime/failure evidence plus
    LLVM, safe-point, named-event, fork/process, procedural-assignment,
    suspending-task, mailbox/semaphore, and ordering-application gates pass
    11/11. Diagnostic catalog and 2,500-line source policy remain green;
    `git diff --check` is clean. Continue with Change 17's portable artifact,
    relocation, native-cache, debugger, callback, trace, and snapshot
    preservation. Preserve the dirty accumulated Batch 152 worktree; do not
    commit, push, inspect hosted CI, or run a sanitizer before Change 20.
21. Change 17 is complete in the same accumulated worktree. Runtime-state
    schema 16 versions the completed procedural operation set, and native
    object-cache schema v87 separates appended shuffle identity from older
    objects. Mailbox/semaphore and event/container-ordering designs survive
    deterministic runtime-state round trips before execution. A moved
    `.fsimlib` remains mapped and runs the shuffle fixture identically through
    interpreter and LLVM, including container snapshots, debugger output,
    signal callbacks, VCD, and native-cache identity. The eight-worker exact-
    LLVM Debug build and full application gate succeed. Library, object/design
    artifact, diagnostic catalog, 2,500-line source policy, LLVM,
    synchronization, ordering, and runtime gates pass 9/9; `git diff --check`
    is clean. Continue with Change 18's cataloged malformed/type/rank/resource/
    lifetime negatives and engine/optimization/cache/multiple-root/restart
    differentials. Preserve the dirty accumulated Batch 152 worktree; do not
    commit, push, inspect hosted CI, or run a sanitizer before Change 20.
22. Change 18 is complete in the same accumulated worktree. Shuffle has
    cataloged malformed-arity, associative/nonintegral receiver, missing-random-
    source, forbidden-key, and corrupt portable-enum negatives. The ordering
    fixture now elaborates two roots, deterministically round-trips runtime
    state, rejects an invalid archive enum before publication, relocates its
    `.fsimlib`, and agrees across O0/O2 interpreter, cold/warm LLVM cache, and
    restarted mapped-library execution. The full eight-worker exact-LLVM Debug
    build succeeds. Diagnostic catalog, 2,500-line source policy, elaboration,
    ordering, runtime, LLVM, full application, fork, container-application, and
    synchronization gates pass 10/10; `git diff --check` is clean. Continue
    with Change 19's public architecture, language support, diagnostics,
    feature-evidence, inventory, release-contract, and restart-handoff sync.
    Preserve the dirty accumulated Batch 152 worktree; do not commit, push,
    inspect hosted CI, or run a sanitizer before Change 20.
23. Change 19 is complete in the same accumulated worktree. Public README,
    architecture, language support, diagnostics, feature evidence, inventory,
    release contracts, and this handoff now describe Batch 152 and retain the
    later program/clocking/SVA/foreign/UVM boundaries. `SV-733` through
    `SV-740` advance the reviewed feature matrix to 1,170 executable rows and
    4,680 evidence cells over 414 exact paths. Matrix digest
    `4c9bc37c9076a6e331ab09386c6d2a9437bdf001b7f5a6857208386bcc274ba6`
    and evidence digest
    `c76f1dbfecd0fc5392f2f7108da2f2d5c06830cd2777952d833821241d8e1fa5`
    are locked into the release contracts. Inventories contain 1,875
    production diagnostics, 570 bounded C/C++ sources, 660 SPDX-owned
    artifacts, and 221 authored test/control files. All 26 documentation,
    conformance, release, inventory, installation, and Linux/Windows
    portability contracts pass; `git diff --check` is clean. Continue with
    Change 20's full non-sanitized exact-LLVM Debug/Release and release gates,
    then commit and push the accumulated Batch 152 work once. Do not run a
    sanitizer or inspect hosted CI for this non-monitoring batch.
24. Change 20 is complete. The final exact LLVM 22.1.8 Debug tree rebuilds with
    eight workers and passes 114/114 tests in 206.72 seconds. The Release tree
    completes all 448 eight-worker build steps and passes 114/114 tests in
    163.74 seconds. Release-only `-O3 -Werror` evidence made the guarded packed/
    scalar element width explicit, and the transition-delay negative now
    expects only the still-illegal negative constant while runtime scalar delay
    remains supported. Both full runs include the 26-contract release prefix,
    diagnostic catalog, 2,500-line source policy, artifacts, LLVM, runtime, and
    every application matrix. Batch 152 is not a ten-batch monitoring boundary:
    no sanitizer ran and hosted CI was not inspected. Closeout is the one
    accumulated commit and push containing this handoff. Continue with Batch
    153 Change 1 after verifying the branch and origin tips match.

## Batch 150 completed checkpoint - 2026-08-05

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

The complete remaining release roadmap is locked through Batch 177:

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
- 172: pinned Accellera SystemC 3.0.2, native TLM 1.0/2.0, a transport-neutral
  worker-ready backend, and complete SystemC signal/port waveform visibility.
- 173: governed SCV 2.0.1 compatibility, randomization/introspection and
  transaction-recording closure.
- 174-177: ABI/artifact/migration freeze, cross-platform qualification,
  release-candidate packaging/documentation, and final `v2.0.0` qualification.

Batches 150, 160, 170, 172, and 173 are the remaining CI-monitoring boundaries.
Only their Change 20 runs the LLVM-disabled sanitizer locally immediately
before the single commit, then pushes and monitors/repairs all
non-documentation GitHub Actions jobs. Hosted CI excludes sanitizer
instrumentation. All other batches run no sanitizer and no hosted CI
inspection.
The v2 language-closure boundary is the standardized digital surface recorded
in the official plan. Accellera SystemC 3.0.2, native TLM 1.0/2.0, SCV 2.0.1,
the opaque worker-ready backend and complete SystemC signal/port debug/trace
visibility are now v2 requirements. VHDL-AMS, proprietary semantics beyond the
explicitly planned compatibility packages, SystemC AMS/CCI, GUI/reverse
execution, the actual post-v2 worker-process partitioner and parallel scheduler,
standalone AOT, and Python/notebook product work remain outside v2 unless the
user changes scope.

- Branch: `codex/v2`, tracking `origin/codex/v2`.
- Baseline: `1462f18`; annotated `v1.0.0` points to `6450599`.
- Current unit: Batch 151, exactly 20 expanded changes. Changes 1-7 are complete:
  the owning packed constant representation, `svconst-v2` width/signed/domain/
  type identity, 128-bit two-/four-state and 96-bit Z evidence, and the checked
  16,777,216-bit limit pass seven focused exact-LLVM Debug gates after an
  eight-worker build. Change 2 adds portable arbitrary-width decimal,
  binary/octal/hex, concatenation/replication, and checked width/work/storage
  accounting with exact 128-bit positives and a bounded large-work rejection.
  Change 3 adds exact wildcard/qualified package import, specparam, distinct
  specialization, wide generate-case, checked range, and lossless mixed-
  generic propagation; nine focused gates pass. Change 4 carries exact wide
  two-/four-state profiles and values through elaborated signals, SimIR, and
  interpreter admission with runtime/LLVM validation; eleven gates pass.
  Change 5 adds exact bounded wide arithmetic/unary semantics, signed divide/
  modulo and negative-power handling, checked destination sizing, unknown
  propagation, signed-overflow rejection, and a multiplicative work-limit
  negative; eleven focused gates pass. Change 6 adds exact wide logical/
  bitwise, equality/wildcard, signed/unsigned comparison, saturating shifts,
  unknown-count, and Logic4/Logic9 sign-extension behavior; eleven focused
  gates pass. Change 7 performs the required below-2,000-line evaluator split,
  then adds exact wide streaming, reductions, all packed selector forms,
  out-of-range Xs, selected constant-function writes, wide conditional merging,
  and positional/default flat patterns; eleven focused gates pass. Change 8
  adds governed builtin/named packed casts, declared-range query preservation,
  arbitrary-width `$clog2`, exact unknown-state policy, a 128-bit interpreter
  cast, and a 58-output interpreter/compiled O0/O2 differential; eleven focused
  gates pass. Change 9 is next: nested packed structs and anonymous packed
  aggregates.
- Completed unit: Batch 150, exactly 20 expanded changes, complete after pushed
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
  push, and hosted non-documentation CI inspection remain. The source-size
  policy now enforces a 2,500-line hard limit with a mandatory below-2,000
  refactor whenever that limit is exceeded. Sanitizers remain local at the
  ten-batch cadence and are excluded from the hosted workflow, including its
  libFuzzer smoke target.
  The sanitizer-free Clang 22 fuzz target passes the exact 20,000-run hosted
  command locally. Exact-LLVM Debug and Release each pass the application plus
  seven policy/release gates 8/8. Pre-change hosted run `31049629546` satisfies
  the requested wait boundary with Linux and clang-cl Windows test-suite
  failures; both Windows configurations fail-fast after entering class
  integration, so bounded subphase traces were included in the first repair
  push. They localized `0xc0000409` after artifact creation to renaming the
  class source while its input stream remained alive. The input stream is now
  destroyed before the Windows rename boundary; exact-LLVM Debug/Release
  `fsim.application` pass locally in 29.33/28.18 seconds. Replacement hosted
  run `31054730031` passes all eleven jobs, including both Windows MSVC and
  both clang-cl test suites.
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
