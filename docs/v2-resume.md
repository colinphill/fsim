<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v2 restart handoff

Read [implementation_plan_v2.md](implementation_plan_v2.md) first; it is the
authoritative v2 batch/status record. Preserve the completed v1 history in
`v1-resume.md`.

## Batch 157 in-progress checkpoint - 2026-08-07

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
60. `FSIM-SV-SEM-202` enforces a deterministic 256-process per-design-unit
    executable assertion limit and diagnoses the 257th directive exactly once.
    Focused evidence proves all four codes and also covers the malformed empty
    property-token guard that prevents secondary diagnostics or crashes.
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
Only their Change 20 runs the LLVM-disabled sanitizer locally immediately
before the single commit, then pushes and monitors/repairs all
non-documentation GitHub Actions jobs. Hosted CI excludes sanitizer
instrumentation. All other batches run no sanitizer and no hosted CI
inspection.
The v2 language-closure boundary is the standardized digital surface recorded
in the official plan; VHDL-AMS, proprietary semantics, full Accellera SystemC
kernel/TLM/AMS/CCI compatibility, GUI/reverse/parallel simulation, standalone
AOT, and Python/notebook product work remain outside v2 unless the user changes
scope.

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
