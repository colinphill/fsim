<!-- SPDX-License-Identifier: Apache-2.0 -->

# fsim engine v4: semantic contract

Status: draft for owner approval (Phase 0 of
[simulation-engine-v4-plan.md](simulation-engine-v4-plan.md), written
2026-10-25). Code and tests cite clauses as `engine-v4-semantics S<n>`.

This document states what the v4 engine (the static kernel) must guarantee,
which LRM clauses ground each guarantee, what the code guarantees today, and
which test witnesses it. It is a contract about observable behavior. It does
not describe the kernel's data structures; those are in the
[progress log](simulation-engine-v4-progress.md).

## 0. How to read this document

Every clause has four parts:

- **Guarantee:** the behavior v4 must exhibit in its end state.
- **Basis:** the LRM clauses, or the plan decision, that require it.
- **Status:** one of
  - *implemented*: the code does this today;
  - *partial*: the code does part of it, and the gap is named;
  - *not yet*: nothing implements it.
- **Witness:** the test that fails if the guarantee breaks. "To add" means
  no test checks it yet.

Terms:

| Term | Meaning |
|---|---|
| Kernel | The static kernel: `src/runtime/simir_static_kernel*.cpp`, planned by `ElaboratedDesign::plan_static_kernel` (`src/elaboration/elaborated_design_static_kernel.cpp`) |
| Member | A process the kernel runs: kind combinational, sequential, once or behavioral |
| Hosted | A process the kernel schedules and the reference evaluator executes (S25); counts as kernel-owned |
| Host | The reference path: the `runtime::simir::Interpreter`, its scheduler, and the reference evaluator (`step_generic`) |
| Reference | The interpreter without the kernel (`--engine interpreter`, `FSIM_STATIC_KERNEL` unset) |
| Oracle | The reference, used as the differential oracle (S22) |
| Defined behavior | Behavior the LRM fixes. Everything else is a race (S18) |

Witness tests (all in `tests/CMakeLists.txt`, labelled `static-kernel`):

| Short name | ctest name | Source |
|---|---|---|
| T-sv | `fsim.application.static-kernel` | `tests/app/static_kernel_test.cpp` |
| T-beh | `fsim.application.static-kernel-behavioral` | `tests/app/static_kernel_behavioral_test.cpp` |
| T-vhdl | `fsim.application.static-kernel-vhdl` | `tests/app/static_kernel_vhdl_test.cpp` |
| T-cone | `fsim.application.cone-fusion` | `tests/app/cone_fusion_test.cpp` |
| Corpus | six-suite LRM sweep on `--engine v4` | `scripts/lrm_corpus.py` |
| Parity | `kernel_parity.py`, `rs_vhdl_parity.py` | `.local-artifacts/simulation-performance/r37-analysis/tools/` (not in the repo) |

Each of T-sv, T-beh and T-vhdl builds one design, runs it with the kernel off
and on, and requires identical transcripts and identical hidden-signal values
mid-run and at the end. T-sv and T-vhdl loop over `SimulationEngine::interpreter`
and `SimulationEngine::compiled`. `compiled` is now an alias of v4 (S1); the
legacy JIT was removed, so the loop compares the reference evaluator with v4.

## S1. Engine selection

- **Guarantee.**
  - `fsim simulate` runs v4 by default.
  - `--engine v4` names it explicitly.
  - `--engine interpreter` runs the reference evaluator only. It stays as the
    differential oracle and as the debug engine.
  - `--engine debug` keeps its meaning (S20).
  - `--engine compiled` is an alias of v4. The legacy per-process JIT is
    deleted (engine-v4 migration, Stage D).
  - `--compiled-processes` and `--aot-scope` are accepted for command-line
    compatibility and ignored.
  - `elaborate --aot` populates the v4 kernel cache.

  | `--engine` | Runs | Native code |
  |---|---|---|
  | `v4` (default), `compiled` | the static kernel, reference evaluator for non-members | LLVM kernel templates (cache `static-kernel/`) |
  | `interpreter` | reference evaluator only | none (unless `FSIM_STATIC_KERNEL=1`) |
  | `debug` | reference evaluator with debugger hooks | none |
- **Basis.** Plan §5 (interpreter kept as oracle), §6 (side by side), Phase 5;
  owner decisions 2026-10-25 (HANDOFF §2 G1, §10 items 2 and 4).
- **Status:** *partial*.
  - There is no `--engine v4`. `src/cli/driver.cpp` accepts
    `interpreter | compiled | debug`.
  - The kernel runs only when `FSIM_STATIC_KERNEL=1` is set and
    `built.cone_fusion` is true (`create_simulation_interpreter`,
    `src/app/application_simulation_impl_setup.cpp`).
  - The kernel's LLVM code is attached whenever the kernel is on, regardless
    of `--engine`. Under `--engine interpreter` the members run as native code
    and the non-members run on the reference evaluator.
- **Witness.** To add: a CLI test that `--engine v4` selects the kernel with
  `FSIM_STATIC_KERNEL` unset, and that `--engine compiled` gives the same
  transcript.

Two reference configurations must not be confused:

| Configuration | Kernel | Members | Non-members | Use |
|---|---|---|---|---|
| Reference | off | none | host evaluator | the oracle (S22) |
| Kernel on, `--engine interpreter` | on | native code | host evaluator | today's census |

## S2. Process membership

- **Guarantee.** A process is a kernel member only when the kernel can run it
  with the behavior of its reference process. Classification is structural. It
  never depends on a design, module or path name.
- **Basis.** Plan §3.1 (classification is structural), §3.2 (standard
  conformance, as-if rule).
- **Status:** *implemented* (membership rules below).
  The planner classifies each process into one of four kinds.

| Kind | Shape (SimIR) | Reference behavior |
|---|---|---|
| combinational | one any-change static wait and a loop jump | 1800 §9.2.2.2 `always_comb`, `always @*`; continuous assignments |
| sequential | one posedge/negedge static wait at the top, a loop jump | 1800 §9.2.2.4 `always_ff`, §9.4.2 event control |
| once | no sensitivity, a terminating `Halt` | 1800 §9.2.1 `initial`; constant initializers |
| behavioral (SystemVerilog only) | any other shape whose operations are all supported | 1800 §9.2.1, §9.4, §9.3.2 testbench threads |

A process is not a member when any of these holds:

- Flags: observed (clocking input samplers), reactive (program-owned),
  postponed, final, not initialized, switch roles, drive strength.
- Registers: string registers (non-behavioral kinds); any non-`logic4`
  SystemVerilog register; VHDL container registers.
- VHDL-2019 units.
- Shape: more than one `WaitSensitivity`, or a wait that is not one static
  sensitivity. Behavioral SystemVerilog adds: an event-variable signal in the
  sensitivity, `static_trigger_regions`, a non-fixed private container.
- Any operation outside `static_kernel_operation_supported`,
  `static_kernel_vhdl_operation_supported` or
  `static_kernel_behavioral_operation_supported`
  (`src/runtime/simir_static_kernel.hpp`).

Operations that keep a process out today (and must be admitted by the end
state, S3):

- SystemVerilog: `WaitFork`, `DisableFork`, `DisableBlock`, `WaitOn` with a
  timeout, postponed `$display`/`$strobe`/`$monitor`, class and dynamic-data
  operations, signal queries other than the current value (`$rose`, sampled
  values, `ticks != 1`).
- VHDL: `ReadSignal` with a kind other than `current` (`'event`, `'stable`,
  `'last_value` and the other signal queries), delayed or non-inertial
  `WriteProjected*`, waits other than one static sensitivity, calls with
  string or container frames.
- **Witness.** T-sv, T-beh, T-vhdl exercise members of each kind. The
  rejection rules have no direct test: to add (a planner-level test that
  checks the refusal reason for each excluded shape;
  `FSIM_PROFILE_KERNEL_PLAN=1` prints the reasons).

## S3. Admission, fallback and the end state

- **Guarantee.**
  - End state: the kernel owns every process of an admitted design. No SimIR
    process runs on the host evaluator (`host=0`). A process the kernel cannot
    run is an admission defect to fix, not a fallback.
  - During the migration (plan §9 D5) two fallbacks exist, and each reports a
    reason code:
    1. whole-design refusal: the kernel is off, and the run is the reference;
    2. per-process host fallback: some processes are not members and run on
       the host evaluator beside the kernel.
  - A fallback never changes defined behavior: a run with fallbacks produces
    the transcript the reference produces.
- **Basis.** Plan §6 and D5; HANDOFF §2 G3 and §10 item 1 (strict `host=0`).
  The plan's text "no per-process mixing" and the code disagree: see
  Appendix B.
- **Status:** *partial*.
  - Whole-design refusal is implemented. Reason codes:

    | Code | Meaning |
    |---|---|
    | `coverage` | code coverage inventory present |
    | `specify` | specify paths or timing checks |
    | `systemc` | SystemC processes or objects |
    | `vcd_control` | a process holds a `VcdControl` operation |
    | `signal_width` | a signal wider than 2^32 - 1 bits |
    | `unknown_dependencies` | some process has accesses the region graph cannot resolve (not a VHDL guarded-driver disconnection, which names its signal, and not a VHDL opaque process when a foreign library or observer makes every signal a host object, S19) |
    | `vhdl_partial` | VHDL kernel, but some VHDL process is not a member (only with `FSIM_STATIC_KERNEL_HOSTED_VHDL=0`; otherwise the process is hosted, S25a) |
    | `vhdl_postponed` | VHDL kernel with a postponed VHDL process (only with `FSIM_STATIC_KERNEL_HOSTED_POSTPONED=0` or `FSIM_STATIC_KERNEL_HOSTED_VHDL=0`; otherwise the process is hosted in the postponed region, S25a) |
    | `vhdl_unowned` | VHDL kernel, a signal written and read by members is not kernel-owned (only with `FSIM_STATIC_KERNEL_HOSTED_VHDL=0`, S25a) |
    | `no_members` | some valid process exists but none is a member, hosted, or a host program |

    A design with no valid process at all, or whose valid processes are all
    host programs, is not refused: the plan is *trivial* (not disabled, no
    specification, census reason `trivial_no_processes` /
    `trivial_host_programs_only`) and the design runs on the ordinary path. The
    kernel has nothing to schedule, so no ordering can differ: every signal is
    a host signal and no process reads or writes one (IEEE 1076-2019 14.7.5,
    1800-2023 4.5 have no process to order). Host programs (S25a) are not
    scheduled processes either; they still count as `host=` until the
    host-program work moves them.

  - Per-process fallback is implemented for SystemVerilog. For VHDL a process
    that is not a member is hosted (S25a); with
    `FSIM_STATIC_KERNEL_HOSTED_VHDL=0` a VHDL design needs every VHDL process
    to be a member (`vhdl_partial`).
  - The profile line (`FSIM_PROFILE_PHASES=1`) reports members, owned
    signals, outputs, inputs, containers, the valid processes (`processes=`),
    those that stay on the host scheduler (`host=`) and those the kernel
    hosts (`hosted=`, S25). `static-kernel-host` rows give the reason each
    host process is outside the kernel; `static-kernel-hosted` rows give the
    reason each hosted process is not a native member (the promotion queue).
- **Witness.** Corpus census (`tools/census.py`) ranks refusals and, once
  added, host-process reasons. G3 is the census showing `host=0` in every
  case. T-vhdl checks that its design is admitted.

## S4. Ownership, boundaries and publication

- **Guarantee.**
  - A signal is kernel-owned only when every writer is a member and no
    non-member touches it other than through a current-value read.
  - A non-member touch is a write, force, deposit, history query or event
    query. It makes the signal a host signal.
  - A kernel-owned signal that a non-member reads is a boundary output. The
    kernel publishes it to the host under the original driver identity, in
    the region its writer would use (S6, S11).
  - A host signal that members read is a boundary input. A change wakes its
    readers in the same time step.
  - An unowned signal behaves exactly as in the reference.
- **Basis.** Plan §3.2 (only unobservable hops are collapsed), §3.5 (arena);
  1800 §4.9.1 (continuous assignment), §4.9.6 (port connections).
- **Status:** *implemented* for the ownership rule.
  - `host_touched` marks signals a non-member accesses by anything other than
    a current read.
  - A resolved net (`sv_wire`, `std_logic`) is owned only when its drivers
    cover disjoint bits, so resolution is the identity (S15).
  - Signals with event variables, implicit drivers, charge strength or SV
    scalar kinds are never owned. Memories are owned when all accessors are
    members.
  - Aggregate alias families (per-element arrays with a proxy) are owned as a
    unit.
- **Witness.** T-sv (netlist, memories, net array), T-cone (hidden nets read
  back), T-vhdl (mixed boundary). Boundary publication under driver identity:
  to add a test that a multi-driver host net still resolves with a kernel
  driver among its sources.

## S5. SystemVerilog combinational evaluation

- **Guarantee.**
  - A combinational member re-evaluates when any input in its sensitivity
    changes. A set of combinational members settles to a fixed point inside
    one Active pass.
  - Evaluation order within the pass is levelized (writer before reader). It
    is one admissible order of Active events.
  - A member does not wake on its own writes while it runs.
  - A combinational loop is evaluated until it settles or the iteration limit
    stops it; loops are placed after the acyclic members.
  - The settled value is independent of the order, for a design with no race
    (S18).
- **Basis.** 1800 §4.4.2.2 (Active events), §4.7 (nondeterminism: any order
  of Active events), §4.9.1 (continuous assignment is an event on operand
  change), §9.2.2.2 (`always_comb`), §9.4.2.2 (implicit sensitivity).
- **Status:** *implemented* with one gap.
  - Levels come from the member write/read graph; members in a cycle take the
    last levels (`simir_static_kernel.cpp`, constructor).
  - Within one level, order is unspecified (a LIFO bucket).
  - Gap: a loop that does not settle raises the error "static kernel
    combinational logic did not settle" after `members * 64 + 1,000,000`
    evaluations. The reference stops at its delta limit instead. Plan §3.1
    says loops and latches become behavioral; that is not implemented.
- **Witness.** T-sv (combinational netlist, case block, net array), T-cone.
  Loop and latch behavior: to add.

## S6. SystemVerilog edge kernels and the NBA commit

- **Guarantee.**
  - A sequential member runs in the Active region when an edge in its
    sensitivity occurs. Edges include those derived from owned signals
    changed inside the kernel (derived and gated clocks).
  - The member evaluates against the settled state, i.e. the values before
    any nonblocking write of the same time step lands.
  - Its nonblocking writes are queued in execution order and commit together
    in the NBA region, after the Active and Inactive regions are empty. Each
    committed write notifies its readers as it lands.
  - After a commit, combinational logic settles again and further edges are
    detected, until nothing remains; then the time step advances.
  - Blocking writes are visible at once (S7).
  - Commit order is the queue order; for two writes to the same bits, the
    later one wins.
- **Basis.** 1800 §4.4.2.4 (NBA events region), §4.9.4 (nonblocking
  assignment), §10.4.2 (nonblocking procedural assignments), §9.4.2 (event
  control, edge definitions), §4.5 (simulation algorithm).
- **Status:** *implemented*. Outputs changed by or after a commit are
  published in the NBA domain, and outputs changed before it in the Active
  domain, so observers never see a combinational result ahead of the
  registers it derives from. Gaps: out-of-range and unknown-index writes
  follow S17.
- **Witness.** T-sv (edge registers with asynchronous reset, derived clock,
  dynamic bit and part-select NBA writes, memory with registered read),
  T-beh. Corpus (ivtest, sv-tests, verilator).

## S7. Region order within a time step

- **Guarantee.** For SystemVerilog members, one time step runs: Active
  (combinational settling, triggered sequential members, ready threads) until
  empty; then Inactive (`#0` resumptions), back to Active when it produced
  work; then NBA, back to Active when it produced work; then the next time
  step.
- **Basis.** 1800 §4.4.2.2–§4.4.2.4, §4.5, §9.4.1 (`#0` delay is the Inactive
  region).
- **Status:** *partial*.
  - Implemented for Active, Inactive and NBA (`sv_drain`, `activate_step`).
  - Not run by the kernel: Preponed, Observed, Reactive, Re-Inactive,
    Re-NBA, Postponed, and the PLI regions. Processes that need them
    (clocking input samplers, program processes, postponed output,
    `$monitor`, `$strobe`, `final`) are not members (S2) and run on the host.
  - The host scheduler still owns time and the other regions; the kernel is
    a pseudo-process activated by it (S10).
- **Witness.** T-beh (delays, `#0` order not isolated). To add: `#0`,
  `$strobe` and NBA ordering test; reactive-region test once admitted.

## S8. Behavioral threads

- **Guarantee.**
  - A behavioral process runs as one or more resumable threads. A thread runs
    until it suspends, then resumes exactly where it stopped, with its
    registers, call stack and automatic frames intact.
  - Every behavioral process starts at time 0 in the Active region.
  - Suspension forms and resumption:

    | Construct | Resumes |
    |---|---|
    | `#n` (n > 0) | at now + n, in the Active region; ties broken by (time, creation sequence) |
    | `#0` | in the Inactive region of the same time step |
    | `@(...)`, `wait (cond)` | when a listed signal changes with the requested edge; edge filters follow 1800 §9.4.2; a resumed thread abandons its other registrations |
    | wait on the member's static sensitivity | likewise |
    | `wait` forever / no event | never |

  - A delay that overflows the representable time never resumes.
  - Blocking writes are visible at once; combinational partitions settle
    after the thread suspends. Nonblocking writes follow S6.
  - Runtime-library output (`$display` family, `%t`, `%b`, `%h`, strings,
    `$time`/`$stime`/`$realtime`, plusargs, `$finish`, `$stop`) is identical
    to the reference's, using the same formatter.
- **Basis.** 1800 §9.2.1, §9.4.1 (delay control), §9.4.2 (event control),
  §9.4.3 (level-sensitive `wait`), §9.5 (process execution threads), §20.3
  (time functions), §21.2 (display), §4.4.2.3 (Inactive).
- **Status:** *partial*.
  - Implemented: `WaitFor`, `WaitOn` (no timeout), `WaitSensitivity`,
    `WaitForever`, `Halt`, calls with automatic frames of packed registers,
    integer ops, immediate output, private fixed arrays, plusargs.
  - Not a member (host): wait timeouts, postponed output, dynamic data
    (queues, dynamic arrays, associative arrays), classes, strings in
    registers other than constants and copies, `event` variables,
    non-`logic4` registers (`real`, int kinds, chandle).
  - Execution tiers: interpreted threads or compiled templates; a compiled
    thread whose register cannot be represented deoptimizes to the reference
    evaluator and continues from the same operation.
- **Witness.** T-beh (delay clock, `repeat`/`@(edge)`, `wait(cond)`, fork/join,
  automatic tasks and functions, arrays, `$display`, `$finish`). Corpus:
  ivtest behavioral cases. Open losses: HANDOFF §4.3 (`always3.1.9C`,
  `disable3.6A`, `fork_join_dis`, `inout`, `tri2`).

## S9. Fork, join and process control

- **Guarantee.**
  - `fork` starts one child thread per branch in the Active region. `join`
    resumes the parent when all children finish; `join_any` when the first
    finishes; `join_none` immediately. The parent resumes in the current
    Active step.
  - Children share the parent's registers (as the reference does) and have
    their own program counter and call stack.
  - `wait fork`, `disable fork` and `disable <block|task>` terminate or await
    the descendant threads exactly as 1800 §9.6 specifies, including threads
    suspended on a timer or an event, and including a disable of a block
    outside the process's lexical scope.
- **Basis.** 1800 §9.3.2 (parallel blocks), §9.6.1 (`wait fork`), §9.6.2
  (`disable`), §9.6.3 (`disable fork`).
- **Status:** *partial*.
  - `Fork`/`ForkEnd` with all three join kinds are implemented.
  - `WaitFork`, `DisableFork` and `DisableBlock` are not admitted, so a
    process using them stays on the host.
  - Forks marked `isolate_register_frame` or `detached` are not admitted.
- **Witness.** T-beh (fork/join of a driver and a checker). To add:
  `join_any`/`join_none`, `disable fork`, `wait fork`, named block disable.

## S10. Time ownership

- **Guarantee.**
  - Time, `$time` and `$finish` observe the same values as the reference at
    every point an observer can see.
  - The kernel may run its own consecutive time steps (time warp) only for a
    closed kernel: every process is a member, there are no host outputs, no
    host-written inputs, and nothing outside the kernel records between time
    steps. A warped step reports the time and delta the scheduler would have
    reported.
  - Warp is refused under: a run limit, active PSL directives, any signal,
    scalar, driver or container change hook, safe-point observers, VPI
    handles or runtime updates, SystemC.
- **Basis.** 1800 §4.3, §4.5 (time advances only when no events remain);
  1076 §14.7.5.3 (simulation cycle).
- **Status:** *implemented* for warp (`FSIM_STATIC_KERNEL_TIME_WARP=0`
  disables it); *not yet* for general kernel-owned scheduling. The host
  scheduler still owns the future-event queue except while warping. Kernel
  members never read `$time` through `ReadSimulationTime`.
- **Witness.** `kernel_parity.py` (Parity). To add: a test that
  `--duration` and an observer disable warp without changing the transcript.

## S11. VHDL exact delta mode

- **Guarantee.**
  - In a VHDL kernel, one kernel round is exactly one VHDL delta cycle.
  - In round k, the processes that resumed because of an event in round
    k-1's update phase execute, in unspecified order, each running to its
    next wait. Their signal assignments are deferred: they affect only the
    update phase at the end of round k.
  - The update phase applies the round's transactions in program order (the
    last to an element wins), then reports each signal whose value differs
    from its value at the start of the round exactly once.
  - A process that writes a signal does not see the new value in the same
    round (no shoot-through), and does not wake on its own update in that
    round.
  - Time advances only when no delta remains at the current time.
  - A kernel that never settles is stopped by the host delta limit (the
    kernel yields to the host at least every 256 rounds).
- **Basis.** 1076-2019 §14.7.5.3 (simulation cycle), §14.7.5.2
  (initialization), §14.7.3.4 (signal update), §14.7.2 (drivers), §10.5.2
  (simple signal assignment), §10.2 (wait statement), §11.3 (process
  statement).
- **Status:** *partial*.
  - Implemented for members with one static wait, zero-delay inertial
    assignments (`WriteProjected*` with delay 0), and shared-variable writes
    (immediate).
  - With `FSIM_STATIC_KERNEL_HOSTED_VHDL=0` a design is admitted only when
    every VHDL process is a member (`vhdl_partial` otherwise) and every signal
    that one member writes and another reads is kernel-owned
    (`vhdl_unowned`). By default a VHDL process that is not a member is hosted
    (S25a) and a signal exchanged between members through the host is
    admitted: the host's update phase of the writer's delta commits it and the
    reader runs in the next delta, which is the reference schedule.
  - Not implemented: delayed (`after`), transport, reject and
    waveform assignments; `wait for`, `wait until`, several waits per
    process, waits inside procedures (S14).
- **Witness.** T-vhdl: delayed clock sampled through an adapter and through a
  plain signal (shoot-through), derived clock, one-delta glitch counted by the
  testbench, shared-variable RAM. A mutation (immediate VHDL writes) makes it
  fail. Also Parity (`rs_vhdl_parity.py`), `FSIM_KERNEL_VERIFY` (S22).

## S12. Delta-depth publication and as-if collapsing

- **Guarantee.**
  - A hop between two kernel-owned VHDL signals may be collapsed (evaluated
    without materializing its delta) only when no construct the standard
    defines can tell the difference. Readers that can tell are: testbench
    processes, `'event`/`'transaction`/`'last_value` users, postponed
    processes, and signals crossing to the host or to another language.
  - Everything observable is published at its reference delta.
  - If conformance cannot be proved, the exact schedule is kept.
- **Basis.** Plan §3.2 (delta depth, as-if), D2; 1076 §14.7.5.3, §16.2.4.
- **Status:** *not yet*. Today every VHDL assignment costs one kernel round,
  so the schedule is the reference schedule. The only collapse is a
  pure-routing port association (`static_kernel_wiring`): the kernel copies
  the source range into the formal in the update phase that changes the
  source, so the formal updates in the same delta (1076 §6.5.7.1,
  §14.7.3.3). An output formal associated with an element or slice of its
  actual is wired the other way: when every conversion driver of the actual
  is pure routing, the kernel copies each formal into its part of the
  actual in the update phase that changes the formal (§14.7.3.2; each
  element has the one driver, so the resolution is the identity, as for
  `writers_ok`). In a mixed kernel a wiring program counts as a VHDL writer.
  A wiring whose source, formal or actual cannot be kernel-owned reverts to
  a host program (for a driver wiring, all of the actual's drivers do).
- **Witness.** T-vhdl covers a wired formal indirectly. To add: a test that
  collapsing keeps `'event`-visible deltas, and one that a non-pure hop is
  never collapsed.

## S13. VHDL signal observers

- **Guarantee.** `'event`, `'active`, `'transaction`, `'last_value`,
  `'last_event`, `'last_active`, `'stable`, `'quiet`, `'delayed` and
  `rising_edge`/`falling_edge` return, at every delta, what the reference
  returns. A signal with such a reader is published at its exact delta (S12)
  and its transaction history is maintained.
- **Basis.** 1076 §16.2.4 (predefined attributes of signals), §14.7.3.4
  (event, active), §14.7.5.3.
- **Status:** *partial*.
  - A process that reads any of these is not a member (`ReadSignal` kind
    other than `current`), so the whole VHDL design is refused as
    `vhdl_partial`.
  - Edge sensitivity on a one-bit signal is carried by the member's static
    sensitivity (S11), not by an observer operation. Whether
    `rising_edge`/`falling_edge` calls inside a body lower to current-value
    reads (and so stay members) was not checked here.
  - Members make only zero-delay inertial writes, which never leave pending
    transactions, so `'transaction` and `'active` have nothing to observe
    for member-written signals.
- **Witness.** To add: kernel-versus-reference test with `'event`,
  `'last_value`, `'transaction` and `'stable` readers.

## S14. VHDL process forms: waits and postponed processes

- **Guarantee.**
  - `wait on`, `wait until`, `wait for`, several waits in one process, waits
    in procedures, and `wait` with timeout resume with the reference
    semantics (1076 §10.2: condition evaluated after each event on the
    sensitivity set; timeout in simulation time).
  - A postponed process executes in the last delta cycle at the current time,
    after all non-postponed processes and signal updates (1076 §14.7.5.3), and
    sees the settled state.
  - Initialization: every process executes once until its first wait, in
    unspecified order, before the first simulation cycle (1076 §14.7.5.2).
- **Basis.** 1076 §10.2, §11.3, §14.7.5.2, §14.7.5.3.
- **Status:** *partial*. VHDL behavioral members (`kind behavioral`,
  `vhdl`): processes that fail the static VHDL shapes run as resumable
  kernel threads on the interpreter tier. Supported: `wait for` (static or
  TIME-valued), `wait on`, `wait until` (a WaitOn loop that re-evaluates the
  condition), each with or without `for T`, bare `wait`, several waits per
  process, waits inside procedures (per-thread call stack), `report`,
  `std.env.stop/finish`, `now`, and the zero-delay inertial assignments of
  the delta mode. A thread runs in the VHDL round with the members, its
  assignments go through the round queue and apply in the update phase; a
  wake (signal event, timeout, `wait for 0 ns`) resumes it in the next
  round; a nonzero `wait for` and every timeout expire in the update phase
  of their time and run in the following delta (`queue_timeout`
  parity). Every thread runs once from operation 0 to its first wait at
  initialization. Still on the host (design refused as `vhdl_partial`):
  postponed processes, string/container registers, `'event`/`'last_value`/
  `'active`, delayed/transport/waveform assignments, and operations outside
  the delta-mode set. `FSIM_STATIC_KERNEL_VHDL_BEHAVIORAL=0` disables the
  tier. *Implemented before*: one static wait with edge or any-change
  sensitivity; initialization to the first wait (`run_at_start`, the
  prologue runs on the reference evaluator and the body from its start in
  compiled code); `once` members for processes with no sensitivity.
- **Witness.** To add per form. The corpus covers them when admitted
  (vests, nvc, vhdl-compliance).

## S15. VHDL drivers, resolution and Logic9

- **Guarantee.**
  - A signal with several drivers takes the value of its resolution function
    applied to the drivers' values (1076 §14.7.3.2). A user-defined
    resolution function, `std_logic`'s `resolved`, guarded signals and
    per-element resolution behave as in the reference.
  - Logic9 stays internal to VHDL values. A value of `U`, `W`, `L`, `H` or `-`
    is preserved through reads, writes, moves and comparisons; where an
    operation treats it as unknown, it does so as the reference does
    (comparisons, arithmetic and branches treat `U` as X).
  - Mapping a signal to Logic4 is allowed only when exact.
- **Basis.** 1076 §14.7.2 (drivers), §14.7.3.2 (driving values), §14.7.3.3
  (effective values), §14.7.3.4; `std_logic_1164`.
- **Status:** *partial*.
  - Resolution is the identity in the kernel: a resolved signal is owned only
    when each element has exactly one driving process, and a resolved
    `std_logic` signal also needs a driver for every element
    (`writers_ok`). Any other multi-driver signal stays a host signal with a
    host resolver.
  - General resolution functions in the kernel are *not yet*.
  - Logic9 storage is implemented as four planes; compiled code tracks `U`
    with a shadow mask and deoptimizes to the reference evaluator for
    `W`, `L`, `H` and `-`.
  - Guarded signals, non-pure-routing conversion drivers and port
    associations keep their programs on the host and make their sources host
    signals.
- **Witness.** T-vhdl (weak `H`/`L`, disjoint resolved drivers, X injection).
  To add: multi-driver `std_logic` with conflicting values, a user resolution
  function, guarded bus.

## S16. Mixed-language boundaries

- **Guarantee.**
  - The cross-language policy in
    [cross-language-semantics.md](cross-language-semantics.md) holds
    unchanged.
  - In one kernel, VHDL members follow delta rounds and SystemVerilog
    members the Active/Inactive/NBA regions, interleaved exactly as the
    reference scheduler interleaves them:
    1. pending SystemVerilog work drains first;
    2. VHDL members triggered for round k run (writes deferred); SystemVerilog
       members and threads woken by round k-1's commit become ready;
    3. SystemVerilog drains again; VHDL members woken by these writes join
       round k+1;
    4. the VHDL round commits; VHDL readers join round k+1; SystemVerilog
       readers and waiters are held for step 2 of delta k+1.
  - A SystemVerilog-origin wakeup stays in the current delta. A
    SystemVerilog process woken by a VHDL update runs in the next delta. A
    VHDL process woken by any change runs in the next delta.
  - When every kernel round is a host delta (`host_boundary_`: hosted or
    host processes at the boundary), the VHDL members and threads woken by
    SystemVerilog work outside a round (pending at activation, or woken by
    inputs after a round settled) are held and the kernel yields, so they
    run in the next host delta together with the hosted VHDL processes the
    same writes wake (`sv_drain_outside_round`). Running them in the next
    iteration of the same activation would put them one delta ahead of the
    hosted processes (`mixed_codec`: a hosted RAM read port saw the
    address its native counter had already advanced).
  - Slots of Logic9 signals read by SystemVerilog code are coerced to Logic4
    (`U` becomes X); publication uses the writer's domain.
  - A SystemVerilog member of a mixed kernel does not require the host
    signals it exchanges to be kernel-owned; only VHDL accesses require a
    closed kernel (S11).
- **Basis.** `cross-language-semantics.md`; 1800 §4.5; 1076 §14.7.5.3;
  no standard orders zero-delay events across languages, so the reference
  scheduler's interleaving is the contract.
- **Status:** *implemented* (`activate_mixed`; `FSIM_STATIC_KERNEL_MIXED=0`
  falls back to the majority language only). Memories are not owned when a
  VHDL member accesses them in a mixed kernel.
- **Witness.** T-vhdl (SystemVerilog testbench over a VHDL design, each
  kernel round its own host delta); Parity on `mixed_throughput`;
  `FSIM_KERNEL_VERIFY` on the mixed fixtures.

## S17. Four-state value semantics

- **Guarantee.** Every operation the kernel evaluates, in any tier (generic
  evaluator, interpreted KIR, compiled KIR/LLVM), returns the bits, X/Z
  planes and errors that the reference value library (`packed_value`,
  SimIR operation semantics) returns. A tier that cannot represent a value
  hands the rest of the activation to the reference evaluator (deoptimization)
  with the registers synchronized in both directions. Specific rules:

  | Case | Required behavior | Basis |
  |---|---|---|
  | SV bit/part select read, index X/Z or out of range | the bits outside the source are X (a part select) or X (a bit select) | 1800 §11.5.1 |
  | SV bit/part select write, index X/Z or out of range | no effect | 1800 §11.5.1 |
  | SV memory element read, index X/Z or out of range | X (or the default value of the element type) | 1800 §11.5.2 |
  | SV memory element write, index X/Z or out of range | no effect | 1800 §11.5.2 |
  | VHDL index outside the bounds (`strict`) | a checked-constraint error with the reference's message | 1076 §5.3.2 |
  | Two-state conversion | X and Z become 0 | 1800 §6.11 |
  | Arithmetic with X/Z operand | all-X result per 1800 §11.4.3 | 1800 §11.4 |
  | Equality with X/Z | `==` is X, `===` is exact | 1800 §11.4.5 |
  | Branch on a value containing X | takes the else path (SV) | 1800 §12.4 |
  | Logic9 compare, arithmetic on `U` | treated as X, as the reference | reference behavior (progress §4, VHDL in the kernel) |

  Errors the kernel raises must be errors the reference raises, with the same
  message and at the same operation.
- **Basis.** 1800 §6.11, §11.4, §11.5.1, §11.5.2, §12.4; 1076 §5.3.2,
  §9.2; plan §3.4.
- **Status:** *partial*.
  - SystemVerilog dynamic selects: read returns X and write is a no-op when
    the selection is not `strict`; strict (VHDL) selects fail with the
    reference message. Implemented in the generic evaluator, the KIR
    interpreter and the compiled tier (non-strict out-of-range writes keep the
    target; strict ones deoptimize).
  - Known defect (HANDOFF §4.3): seven corpus cases (`pr596`,
    `pr3054101a`, `undef_lval_select`, nvc `select4`, others) fail with
    "dynamic packed index unknown/high-Z" under the kernel where the
    reference does not. The path is not identified here; it is a
    kernel bug under S17, not an LRM allowance. Container (memory) element
    writes with an unknown index take `known_index`/`fixed_offset`, which
    raise "container index is out of range" in the generic path; whether that
    matches the reference for X indexes has not been checked.
  - Wide (> 64 bit) registers still go through the reference evaluator.
- **Witness.** T-sv (dynamic bit and part-select NBA writes, net array
  dynamic read), T-vhdl (X injection), `FSIM_KERNEL_VERIFY` for VHDL. To add:
  a targeted X/Z/out-of-range index table for read, write, memory, VHDL.
  Corpus: the seven cases above.

## S18. Races (decision D4)

- **Guarantee.**
  - Where the LRM leaves an order undefined, v4 uses one documented
    deterministic order. It need not match any other simulator or the
    reference. The same input always gives the same output.
  - Transcript parity against every oracle is required only for race-free
    designs (deterministic fixtures, plan D4).
  - A kernel versus reference divergence on defined behavior is a kernel
    bug. A divergence on a race is recorded in the progress log; it is not a
    corpus exclusion unless the owner agrees.
- **Basis.** 1800 §4.6 (determinism), §4.7 (nondeterminism), §4.8 (race
  conditions); 1076 §14.7.5.3 (process order within a cycle is unspecified).
- **The order, as implemented (the contract for undefined orderings):**

  | Choice the LRM leaves open | v4 order |
  |---|---|
  | Order of combinational members in one Active pass | by level (writer before reader); inside one level unspecified but deterministic for a given build |
  | Order of triggered sequential members | ascending member index (elaboration process order) |
  | Order of ready behavioral threads | the order they became ready (FIFO) |
  | Order of timers firing at one time | by (time, creation sequence) |
  | Order of threads vs members in Active | members first, then threads, repeated until quiet |
  | Same-bit NBA writes in one time step | queue order, the last one wins |
  | Blocking write at a clock edge vs a sampler | the blocking write is visible to every member that runs after it in the order above |
  | VHDL processes in a round | ascending member index |
  | Same-time `$finish` and printing of several threads | thread order above; output of finishing instances within a step may differ from other simulators and is compared as a set |
  | Time-0 order of VHDL initialization vs SystemVerilog `initial` | all VHDL initialization in the first VHDL round; the reference interleaves differently (progress §5, item 3) |

- **Status:** *implemented* (the order is deterministic); *partial* for
  documentation: this table is the first statement of it. Excluded race
  cases that pass under the kernel by chance stay excluded (HANDOFF §10
  item 3).
- **Witness.** Corpus (ivtest race cases stay excluded). To add: a
  determinism test that runs one racy design twice and requires identical
  output.

## S19. Observation and access classes

- **Guarantee.** Each signal has one access class, decided before the run
  from the access plan (default run, trace selection, VPI/VHPI/Tcl access
  flags, force/deposit targets, coverage options):

  | Class | Storage | What the contract requires |
  |---|---|---|
  | Unobserved internal net | none (in kernel registers) | cost zero; no observable difference |
  | Readable on demand (default for named nets) | arena slot | a read at a quiescent point returns the exact current value the reference would return, by recomputing the net's cone if needed |
  | Value-change observed (trace, VPI callback, `$monitor`) | arena slot plus a change record | every change is reported at its reference time and delta, with the reference's before/after values |
  | Writable (force, deposit, VPI put) | arena slot plus a force mask | the force/deposit/release semantics of 1800 §10.6 and 1076 §10.5.2, §14.7.3.2 hold; the partition reads through the mask |

  A request outside the compiled plan (a debugger forcing an optimized net)
  recompiles the affected partition as behavioral at the next safe point. It
  never turns the whole engine off.
- **Basis.** Plan §3.6, D3; 1800 §10.6 (procedural continuous assignment,
  force/release), §21.7 (value change dump), §36–§38 (VPI); 1076 §10.5.2, §14.7.3.2.
- **Status:** *partial*.
  - *Readable on demand* is implemented (`materialize`, reached through the
    interpreter's `prepare_signal_observation` hook): a hidden owned signal
    is written to the host under its driver identity when something reads it.
  - *Value-change observed* and *writable* are *not yet*. Today a signal that
    needs either is not owned, because a non-member touch makes it a host
    signal (S4), and a design-level feature makes the whole kernel off
    (S20).
  - Per-partition recompile at a safe point is *not yet*.
- **Witness.** T-sv, T-vhdl, T-cone (hidden signals read back mid-run and at
  the end: the on-demand class). To add: observed and writable classes.

## S20. What disables the kernel today

- **Guarantee.** End state: none of the items below turns the kernel off;
  each moves the affected signals to the observed or writable class (S19) or
  to a service adapter. Until then the list is the exact set of gates, each
  with a reason code.
- **Basis.** Plan §3.6, §5 (adapters), Phase 4; HANDOFF §2 G3.
- **Status:** *implemented* as gates (the kernel is off for the whole run):

  | Gate | Where | Reason |
  |---|---|---|
  | `--engine debug` | `application_run_commands.cpp` (`cone_fusion` false) | debug engine |
  | Code coverage | same, and planner `coverage` | coverage |
  | Trace file with tracing enabled | same | VCD/FST/other trace |
  | Any VPI or VHPI module, or any `--sv-lib` (DPI) | same (`foreign`) | foreign interface may read and write any object |
  | `$dumpvars` and other `VcdControl` operations | planner `vcd_control` | tracing control in the design |
  | Specify paths, timing checks (SDF) | planner `specify` | timing |
  | SystemC | planner `systemc` | co-simulation |
  | Unresolvable accesses | planner `unknown_dependencies` | fail closed |
  | Wide signals (> 2^32 - 1 bits) | planner `signal_width` | representation |

  Access plan (`StaticKernelAccessPlan`, P3): unless
  `FSIM_STATIC_KERNEL_ACCESS_PLAN=0`, the coverage, trace, foreign-library,
  `vcd_control` and `specify` rows are no longer gates. Foreign libraries
  and trace/VCD observers make every signal and container a host object
  (members write it through the host commit path, read it as a boundary
  input; no unwritten or sole-writer input shortcuts for foreign). Module
  paths and timing checks mark their terminals, conditions, notifiers and
  delayed copies host signals. Coverage leaves processes with
  `CodeCoverageHit` as host processes. The debug engine, `systemc` and
  `signal_width` rows stay gates. The run reports
  `fsim-profile: static-kernel-access ...`.

  The gate profile line cannot yet distinguish a kernel-off run caused by
  these flags from one with no simulate step (stage A of the handoff adds it).
- **Witness.** Corpus census (`not planned` column). To add: a test per gate
  that the run is correct and the gate reason is reported.

## S21. Initialization order

- **Guarantee.**
  - Before time 0, package and static initializers run to completion, in
    their dependency order, as ordinary SimIR processes; a static initializer
    that does not finish synchronously is an error in the same cases as in
    the reference.
  - At time 0, every member starts as its reference process starts:
    combinational members flagged `run_at_start` evaluate once; `once`
    members run; behavioral threads start in the Active region; VHDL members
    run to their first wait in the first round (1076 §14.7.5.2).
  - Values signals and variables hold at the start are their declared
    initial values: variable initializers run before any `initial` or
    `always` block starts (1800 §6.8), and the kernel's arena starts from the
    same values. The kernel publishes only what differs from the
    scheduler's initial value.
  - No member observes a value that the reference would not yet have
    assigned. A wired formal starts at its source's initial value (1076
    §14.7.5.2), and a value deposited before the first cycle (a VHPI
    start-of-simulation callback) is kept.
- **Basis.** 1800 §6.8, §4.5 (initial procedures start), §9.2.1; 1076
  §14.4.2.10 (package elaboration), §14.7.5.2.
- **Status:** *partial*. The time-0 sequence above is implemented in
  `StaticKernel::initialize`. Package static initializers are run by the
  host (`simir_interpreter.cpp`), and 37 corpus cases (`sv_export1..6`,
  `sv_ps_*`, `sv_type_param1..7`, verilator `t_package`) fail with the
  kernel on: "invalid SimIR package static initializer process metadata" (35)
  and "did not finish synchronously" (2). The metadata check requires the
  initializer's process fields, which the kernel path changes; the root cause
  is not established in this document and is the largest group in
  HANDOFF §4.3.
- **Witness.** T-sv (memory with an initializer loop), T-vhdl (process
  variables with initializers). Corpus: the 37 cases above. To add: a
  package-initializer ordering test.

## S22. Differential oracle

- **Guarantee.**
  - The frozen interpreter is the oracle. `--engine interpreter` with the
    kernel off must always be available.
  - Every v4 gate compares against it on transcripts, settled values at each
    time step, and value-change streams for observed signals, keyed by time
    step plus observable delta. External oracles (xsim outside the sandbox,
    Verilator for SystemVerilog, nvc for VHDL) are consulted for 4-state and
    VHDL behavior.
  - `FSIM_KERNEL_VERIFY=1` replays every compiled VHDL member run on the
    reference evaluator from the same state, and compares writes and
    registers; a mismatch is a kernel bug and is counted.
  - `FSIM_KERNEL_VERIFY_BODIES` (compiled bodies) and
    `FSIM_KERNEL_CHECK_INPUTS` (input revisions) are further diagnostics; their
    exact checks were not audited for this document.
- **Basis.** Plan §5, §6 (differential gating), Phase 0; progress §3.
- **Status:** *partial*. The VHDL replay and the parity scripts exist.
  Missing: the interpreter-versus-v4 harness keyed by time step and delta,
  value-change-stream comparison, and a SystemVerilog replay (the verify mode
  covers VHDL runs only).
- **Witness.** T-sv, T-beh, T-vhdl (transcript and hidden-value
  comparison on both engines); Parity (`kernel_parity.py mixed_throughput`,
  `original_throughput`; `rs_vhdl_parity.py`); the six-suite sweep compared
  with the `b96x1` pass set (`cmpb.py`).

## S23. Run control, errors and limits

- **Guarantee.**
  - `$finish`, `$stop`, `$fatal`, assertion failures, VHDL `report`/`assert`
    severities and `std.env.stop/finish` stop the run with the same exit
    status, output and final time as the reference.
  - A design that never settles stops with a bounded error; the kernel does
    not hang.
  - A member's runtime error is reported with the reference's message and
    process name.
- **Basis.** 1800 §20.2 (simulation control), §21.2; 1076 §10.3 (assertion),
  §10.4 (report).
- **Status:** *partial*. `Stop`/`Halt` and VHDL assertions are implemented
  (`simulator_status`, stop at the warped time). Limits: the SystemVerilog
  edge loop limit is 100,000 iterations and settle stops after
  `members * 64 + 1,000,000` evaluations, each raising a kernel error whose
  text differs from the reference's delta-limit message. VHDL stops at the
  host delta limit.
- **Witness.** T-beh (`$finish`). To add: limit messages; severity matrix.

## S24. Persisted artifacts

- **Guarantee.** A persisted kernel plan, specialization, compiled body or
  native image is used only when its identity (build, planner knobs such as
  `FSIM_STATIC_KERNEL_BEHAVIORAL`, `FSIM_STATIC_KERNEL_MIXED` and
  `FSIM_STATIC_KERNEL_HOSTED`, specializer,
  compiler and canonicalization identity) matches. Loading never changes the
  transcript: a run from a cache equals a run that plans and compiles from
  scratch.
- **Basis.** Plan §3.7 (cacheable); owner decision 2026-10-08 (the simulate
  step trusts a checksummed artifact; re-derivation checks run in verify mode
  and ctest).
- **Status:** *implemented*.
- **Witness.** T-vhdl (the `aot` and `loaded` runs equal the reference).

## S25. Hosted processes

- **Guarantee.**
  - A SystemVerilog process that is neither a native member nor a host
    program is *hosted*: it keeps its own `ProcessState` and its operations
    run on the reference evaluator (`Interpreter::Impl::execute`), so its
    behavior is the reference's by construction. The kernel owns its
    scheduling. Every wake the host scheduler would queue for it is handed to
    the kernel, and the kernel runs it at the point of its region loop where
    the reference scheduler runs it:
    - Active region: in rounds, in stable ProcessId order; a process woken
      while a round runs is run in the next round (IEEE 1800-2023 4.4, 4.7);
    - Inactive region (`#0`): after all Active work, including the host's, and
      before any NBA commit (4.4, 4.5);
    - timers: at their time, in stable order, abandoned when a disable
      redirects the process (9.6.2);
    - next delta (a wake from another scheduling domain, the time-zero late
      starters): in a later delta than the one that queued it.
  - Hosted execution is never re-entrant: the process runs inside the kernel's
    own host callback, queues every wake it causes, and runs none of them from
    inside `execute`.
  - A hosted process reads and writes host signals only (the planner's
    host-touched rule), through the reference paths; the kernel sees its
    writes as it sees any host process's (input scan, publication).
  - Fork children of a hosted process are hosted.
  - A design with hosted processes has no kernel-owned time (S10): the warp
    needs a closed kernel.
  - Region model (Phase 4). The processes of the Observed, Reactive and
    Postponed regions, `final` blocks and package static initializers are
    hosted as well. Hosted wakes of those regions wait in per-region ready
    lists (observed, reactive, re-inactive, postponed). Each non-empty list
    owns a *doorbell*: a host task of that scheduler phase. The host
    scheduler restarts at the first non-empty region after every region it
    runs (the order of IEEE 1800-2023 4.4, 4.5, Figure 4-1: Active,
    Inactive, NBA, Observed, Reactive, Re-Inactive, Re-NBA, Postponed), so the
    kernel runs each list exactly where the reference scheduler runs those
    wakes, after the kernel's own Active and NBA work and the host's earlier
    regions; the kernel never reorders regions itself (same pattern as the
    deferred NBA commit and the hosted Inactive region). A wake made while a
    region runs joins that region's next round (a new doorbell); a `#0` made
    while a reactive-region process runs is a Re-Inactive wake (4.4, 4.5);
    `WaitRegion` into the Observed, Reactive, Re-Inactive or Postponed
    region is a kernel wake of that region's list. Within a list processes
    run in stable order.
  - Package static initializers (IEEE 1800-2023 6.21, 26.3) are hosted
    members: `Interpreter::start()` still runs them synchronously in
    startup order before any process, but through the kernel
    (`execute_hosted_startup`), as it does the prearming of static waits.
  - Final blocks (9.2.3): `Interpreter::run/finish` call
    `StaticKernel::run_finals`, which forgets the work the scheduler
    discarded (lists, timers, doorbells, the kernel's own deferred-NBA and
    host-process flags) and queues each hosted final block at the end time;
    the activation runs them as Active-region processes.
  - Not moved (service tasks, no SimIR process): the end-of-time-slot and
    postponed publication closures of `$strobe`, `$monitor` and sampled-value
    publishing (`schedule_end_of_time_slot`), VPI/VHPI callbacks, and the
    scheduler's own timed events. They run in the host's Postponed region
    beside the kernel's doorbell. Kernel-owned time (a warp over every
    heap) is not implemented: a design with hosted processes steps through
    the host scheduler's time (S10).
  - `FSIM_STATIC_KERNEL_HOSTED_REGIONS=0` restores the host scheduler for
    these processes (census reasons `hosted_region_<phase>`,
    `package_static_initializer`). Hosted VHDL processes are S25a.
- **Basis.** Plan §3.4 (behavioral fallback on the reference evaluator);
  HANDOFF §2 G3 (no SimIR process runs on the host evaluator); IEEE 1800-2023
  4.4 to 4.9 for the region order.
- **Status:** *partial*. SystemVerilog hosted processes of every region are
  implemented (`src/runtime/simir_static_kernel_hosted.cpp`,
  `FSIM_STATIC_KERNEL_HOSTED=0` restores the host fallback). A hosted
  process's signal writes still go through the host scheduler's update
  queues. The kernel does not own them yet.
- **Witness.** Corpus on `--engine v4` with and without
  `FSIM_STATIC_KERNEL_HOSTED=0`; the census `hosted=` rows. A dedicated
  `static_kernel_hosted_test.cpp` is to add.

## S25a. Hosted VHDL processes

- **Guarantee.**
  - In a VHDL or mixed kernel, a VHDL process that is not a native member and
    not a host program is *hosted*. The kernel schedules it, the reference
    evaluator executes it (S25), and it runs in the delta cycle where the
    reference scheduler runs it:
    - initialization: its first run is part of the kernel's first round
      (1076-2019 14.7.5.2). `Interpreter::start` does not run the host
      stub's prearmed wait; it queues the stub's first activation for the
      scheduler's first delta with every hosted process's initial run, so a
      hosted process initializes in the same delta as the members and
      threads, and its assignments take effect one delta later, as on the
      reference. (Run inside `start`, the first round would count as a delta
      of its own and the kernel's threads would run one delta ahead of the
      host's updates: nvc issue1143.);
    - a signal event, a `wait for` timeout and a `wait for 0 ns` resume the
      process in the delta after the update phase that caused them, together
      with the members and threads the same update wakes (14.7.5.3, 10.2);
      the host's change fanout still detects the event and hands the wake to
      the kernel (`queue_next_delta`, which never runs a VHDL process in the
      delta it was woken in, whatever the writer's language). Processes with
      the same static sensitivity form a *cohort* that the host wakes with one
      task; a hosted member of a cohort is never run by that task, the cohort
      wake hands it to the kernel on its own
      (`queue_static_cohort_next_delta`, `queue_static_active_current`);
    - its signal assignments are not applied by the process: they are the
      host's staged updates (`stage_update*`, `schedule_projected`,
      `schedule_inertial`, `after` waveforms), so driver, projected-waveform
      and resolution semantics (14.7.2, 14.7.3) are the reference's own and
      are not duplicated. They are applied in the update phase of the delta
      in which the process ran, after the kernel round's own commit.
  - One kernel round is one host delta whenever a design has hosted VHDL
    processes (`host_boundary_`): the kernel yields to the host after each
    round that leaves work, so hosted writes commit, and their wakes are
    released, at the reference delta.
  - A hosted process reads and writes host signals (the planner's
    host-touched rule). A kernel-owned signal it reads or waits on is a
    boundary output published at the round that changed it. A signal a member
    writes and another member reads may cross the host (no `vhdl_unowned`
    refusal).
  - A host signal that members read and no member writes (written by hosted
    processes, forces or a testbench) is a kernel input. An event on it is an
    event even when its value after the update phase equals the value the
    kernel scanned last: two force assignments of one cycle apply one after
    the other, 1 to 2 to 1, and the processes sensitive to the signal resume
    (10.5.2.1, 14.7.3.1). The kernel compares the host's event stamp of the
    signal besides its value (`refresh_input`) and wakes the readers.
  - The language of a design with no native member is that of the majority of
    its valid processes (generic scheduling domain: VHDL), so a design whose
    VHDL processes are all hosted is admitted through the synthetic host (no
    `no_members` refusal). Host programs stay on the host (census reason
    `port_program`): they are not scheduled processes. The VHDL processes
    that stay off the kernel are exactly: host programs, compact constant
    drivers (no full process state).
  - A postponed VHDL process (11.3, 14.7.5.3) is hosted and waits in the
    kernel's postponed region list, which reuses the SystemVerilog region
    doorbell (S25): a host task of the postponed phase that runs after the
    last delta cycle of the time step. A wake of such a process (a signal
    event or a timeout) goes to that list at once, as the host scheduler puts
    it in the end-of-slot queue; its initial run (14.7.5.2) is an ordinary
    Active run of the first round. A postponed process cannot cause a delta
    cycle (14.7.5.3), so running the list in one go, ordered by process id as
    the end-of-slot queue is, equals running its entries one at a time.
    `FSIM_STATIC_KERNEL_HOSTED_POSTPONED=0` restores the `vhdl_postponed`
    refusal.
  - A guarded-signal driver disconnection (6.9, 10.5.2.2; the static call
    `@vhdl-disconnect:<signal>:<delay>`) is a known driver write to its
    signal for the access analysis; the process is hosted. A VHDL opaque
    process (a VHPIDIRECT foreign subprogram call) no longer refuses the
    design when a foreign library or an observer is attached, because every
    signal is then a host object.
  - Host programs (port-association resolvers, conversion drivers) are not
    scheduled processes: the host's update commit runs them inline
    (`run_vhdl_resolver`), so there is nothing to schedule and they stay host
    programs.
  - A design with hosted processes has no kernel-owned time (S10).
- **Basis.** 1076-2019 14.7.5.2 (initialization), 14.7.5.3 (simulation
  cycle), 14.7.2 (drivers), 14.7.3 (signal update), 10.2 (wait statement),
  11.3 (process statement).
- **Status:** *partial*. Scheduling is kernel-owned
  (`simir_static_kernel_hosted.cpp`, the VHDL branch of `activate_step` and
  `activate_mixed` in `simir_static_kernel.cpp`, the `queue_*` guards in
  `simir_state.cpp`). *Not yet* (workstream C5b, kernel-owned update queues):
  the host's pending-update queue and `after` waveform tasks still live in
  the host scheduler's update phase; the kernel does not commit or order
  them itself. The hook points are `Impl::schedule_update_commit`
  (`simir_scheduling_stage.cpp`) and `update_commit_callback`: the kernel
  would call the callback at the end of its round instead of the scheduler's
  update phase.
- **Witness.** Corpus (vests, nvc, vhdl-compliance, rs-vhdl) on `--engine v4`
  with and without `FSIM_STATIC_KERNEL_HOSTED_VHDL=0`; `FSIM_KERNEL_VERIFY=1`;
  `rs_vhdl_parity.py`; the census `hosted=` rows of VHDL designs.

## S25b. Commit programs and implicit-signal drivers

- **Guarantee.**
  - A *commit program* is a SimIR program the host update commit runs inline
    and nobody schedules: a VHDL port-association resolver or conversion
    driver (`run_vhdl_resolver`; the conversion program and its halted stub),
    and the halted adapter process of a SystemVerilog inout or switch
    connection (`switch_source`/`switch_target`, solved by the network
    commit). It has no wake, no `ProcessState` run and no position of its
    own: it executes at one fixed place of the update phase, when the signal
    it serves has a staged update (1076-2019 14.7.3: effective value,
    conversion function, resolution function, in that order, once per
    signal per update phase; 1800-2023 23.3.3 and 28: port coercion and
    net-network solution).
  - The kernel plan owns each commit program as a plan row, not as a host
    process: census reasons `commit_program` (resolver/conversion program),
    `commit_program_stub` (its halted stub) and `commit_program_switch`
    (inout/switch adapter) are rows of kernel-owned processes (`hosted`
    group, not counted in `host=`). The kernel takes part in the commit
    through its delta boundary: a design with commit programs is a
    `host_boundary_` design whenever a commit program serves a host signal
    (the planner marks the sources and targets host-touched), so every kernel
    round is one delta and the commit of that delta follows the round, in
    the reference position.
  - Implicit-signal drivers (`S'DELAYED`, `S'TRANSACTION`, `S'STABLE`,
    `S'QUIET` helpers named `<signal>.$implicit_driver`) are real scheduled
    processes (SystemVerilog scheduling domain, no initial run). A VHDL
    kernel hosts them (S25): the wake is the host fanout's next-delta wake
    for another-domain origin (1076-2019 14.7.5.3), the writes are staged
    host updates. A design with no native member counts them as VHDL
    processes when it picks its language. A VHDL or mixed kernel also hosts
    the Active-region SystemVerilog-domain processes it cannot run natively
    (a Verilog process with a register of a VHDL value kind, for example)
    the same way: a wake from the other domain runs in the next delta, one
    from its own domain in the current Active region, as on the host.
    `FSIM_STATIC_KERNEL_HOSTED_IMPLICIT=0` restores the host for both.
  - A design whose only processes are commit programs is admitted
    trivially (`trivial_host_programs_only`); its programs are counted as
    commit-program rows like those of a planned kernel.
- **Basis.** 1076-2019 14.7.3, 14.7.5.3, 6.5.7; 1800-2023 23.3.3, 28.
- **Status:** *partial*. Direction (b) of C5: the commit program still
  executes inside the host's update callback; the kernel does not call
  `update_commit_callback` itself (C5b, S25a status). The accounting is
  honest only while that callback runs at every kernel delta boundary of the
  design, which holds because a commit program's signals are host-touched, so the members' accesses to them are boundary inputs/outputs and `host_boundary_` is set (to verify by census on the listed cases); a SystemVerilog
  kernel yields to the host's update phase by construction (the NBA/update
  region follows the Active region the kernel runs in).
- **Witness.** Census of the nvc port cases (`elab8`, `elab19`, `issue112`,
  `issue405`, `conv2`), the vests port cases, ivtest `inout2`, `br965`;
  `implicit1..3`; every output equal to `--engine interpreter`.

## Appendix A. Environment switches that affect semantics

| Variable | Effect |
|---|---|
| `FSIM_STATIC_KERNEL=1` | turn the kernel on (becomes the default with `--engine v4`) |
| `FSIM_STATIC_KERNEL_BEHAVIORAL=0` | keep behavioral processes on the host |
| `FSIM_STATIC_KERNEL_MIXED=0` | plan the majority language only |
| `FSIM_STATIC_KERNEL_HOSTED=0` | keep non-member SystemVerilog processes on the host scheduler (S25) |
| `FSIM_STATIC_KERNEL_HOSTED_REGIONS=0` | keep observed, reactive, postponed, final and package-initializer processes on the host (S25) |
| `FSIM_STATIC_KERNEL_HOSTED_VHDL=0` | keep non-member VHDL processes on the host and refuse such designs (`vhdl_partial`, `vhdl_unowned`; S25a) |
| `FSIM_STATIC_KERNEL_HOSTED_IMPLICIT=0` | keep implicit-signal drivers and other SystemVerilog-domain processes on the host in a VHDL kernel (S25b) |
| `FSIM_STATIC_KERNEL_HOSTED_POSTPONED=0` | refuse designs with a postponed VHDL process (`vhdl_postponed`) instead of hosting it (S25a) |
| `FSIM_STATIC_KERNEL_NATIVE=0` | no LLVM code; interpreted KIR |
| `FSIM_STATIC_KERNEL_SPECIALIZE=0` | no VHDL body specialization |
| `FSIM_STATIC_KERNEL_TIME_WARP=0` | no kernel-owned time |
| `FSIM_KERNEL_VERIFY=1` | replay compiled VHDL runs on the reference evaluator |
| `FSIM_PROFILE_PHASES=1`, `FSIM_PROFILE_KERNEL_PLAN=1` | print the admission line and the rejection reasons |

All switches must leave defined behavior unchanged; only speed and
diagnostics may differ. (About 45 `FSIM_STATIC_KERNEL_*` and
`FSIM_KERNEL_*` switches exist; the rest tune performance.)

## Appendix B. Disagreements between this contract, the plan and the code

1. Plan §6 says v4 admits a design whole, with no per-process mixing. The
   code admits partially: SystemVerilog non-members run beside the kernel on
   the host (S3). HANDOFF §10 makes strict `host=0` the end state.
2. Plan §3.2 describes VHDL delta-depth collapsing. The code has none (S12);
   progress §3 lists it as not started.
3. Plan §3.6 says no request disables the whole engine. The code disables the
   kernel globally on coverage, trace, foreign libraries, `$dumpvars`,
   specify and SystemC (S20), and has only the readable-on-demand class (S19).
4. Plan §3.1 says combinational loops and latches become behavioral. The code
   levels them last and fails after an iteration limit (S5).
5. Plan §3.5 says the kernel owns Active, Inactive, NBA, postponed and the
   VHDL cycle with a timing wheel. The code runs as a pseudo-process under the
   host scheduler; only warp (S10) moves time ownership into the kernel.
6. Progress §4b says the behavioral tier is opt-in behind
   `FSIM_STATIC_KERNEL_BEHAVIORAL=1`; the code default is on
   (`=0` disables), as `simir_static_kernel.hpp` states.
7. The header comment of `elaborated_design_static_kernel.cpp` says members
   are SystemVerilog processes; VHDL and behavioral members were added later.
8. The tests T-sv and T-vhdl loop over `SimulationEngine::compiled`; since the
   legacy JIT was deleted that loop runs v4 (S1).
