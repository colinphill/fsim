<!-- SPDX-License-Identifier: Apache-2.0 -->
# Design-specific JIT scheduling and update paths

Assessment date: 2026-09-27.

Status: architecture findings and recommendations only. This document does
not authorize implementation, change campaign priorities, or claim measured
performance improvements. The source assessment was read-only; no builds,
profiles, or benchmarks were run for it. The checkout contains concurrent
optimization work, so implementation must recheck the relevant source and
campaign state.

## Recommendation

Generate design-specific dispatch and update functions for proven static
process groups, while retaining the existing outer event scheduler. Begin
with one frequently executed static cohort family, then specialize its update
and fanout paths. Consider cross-process fusion only after these smaller
steps demonstrate a cold end-to-end benefit and preserve event semantics.

The core opportunity is to compile decisions already answered by elaboration:
process membership, signal widths, driver ownership, sensitivity edges,
storage locations, and destination scheduling phases. Generated code would
handle changing values and readiness state without repeatedly interpreting
that metadata.

This is feasible in principle. Verilator's
[scheduling implementation](https://github.com/verilator/verilator/blob/master/docs/internals.rst)
provides a public precedent for generating evaluation functions from static
dependency and scheduling analysis. It is architectural precedent, not proof
that its transformations preserve fsim's full language and observation
contracts.

## Existing fsim machinery

The source examined already implements part of this architecture:

- [LLVM cohort generation](../src/compiler/llvm_jit_execution.cpp) emits
  `fsim_process_cohort_*` functions containing calls to specific native
  process functions. Generated code can manage queued, waiting, and process
  status fields and check for static-wait completion.
- [The executor adapter](../src/app/application_executors.cpp), particularly
  `LlvmProcessExecutor::resume_cohort`, still performs membership and binding
  checks, prepares native entries, batches buffered updates, and interprets
  completion results.
- [Runtime cohort execution](../src/runtime/simir_execution.cpp), in
  `Interpreter::Impl::execute_static_cohort`, restores process context,
  handles deferred executors, groups compatible members, and prepares
  execution entries.
- [Experimental native static regions](../src/runtime/simir_execution_part2.cpp),
  built by `build_native_static_regions`, group eligible processes connected
  through native-publishable signals. The inspected eligibility rules exclude
  processes belonging to static sensitivity cohorts with two or more members.
  Enabling this path alone would therefore miss the large cohorts identified
  by P34.
- [Native publication eligibility](../src/runtime/simir_state.cpp), including
  `can_publish_native_word_prevalidated`, already guards semantic and runtime
  conditions such as driver routing, width, dynamic dependencies, external
  values, and force state.

The next architectural step is to specialize the work surrounding compiled
process bodies. Generating another wrapper around the same generic adapter
would not by itself eliminate that work.

The direction also overlaps the existing
[Batch 193 plan](implementation_plan_v3.md#batch-193---fast-runtime-paths-benchmarks-and-v32-release):
direct compiled state access, scheduler dispatch specialization, operation
fusion, and runtime-invariant metadata specialization. This assessment does
not move that batch boundary or advance its release commitments.

## Evidence and its limits

The following observations come from the current campaign document and its
linked diagnostic receipts. They were not independently rerun for this
assessment.

### Cohort dispatch

[P34](performance-campaign.md#p34-actual-cohort-and-process-shape-attribution)
records 236,550,074 native member resumes in the full original throughput
diagnostic, including 121,949,776 cohort member resumes. All cohort member
resumes ended in a static wait. There were 3,451,363 cohort calls, averaging
35.33 consumed members per call.

Of those cohort member resumes, 97,135,072, or 79.7%, belonged to 541
sensitivity keys with 64–127 registered members. These repeated shapes are
plausible targets for specialized dispatch and completion handling.

Registered membership is not the distribution of ready members per call.
Nor does an observed static-wait outcome prove that other inputs cannot take
another boundary. Eligibility must come from source analysis or guarded
execution, with a correct exit for every other outcome.

### Update topology

[P32](performance-campaign.md#p32-owned-bit-driver-topology-and-route-counts)
attributes all 210,885,738 observed resolved or unchanged-resolved slots to
9,258 SystemVerilog wire signals with pairwise disjoint static ownership
regions covering every bit. Most of these signals have eight one-bit
writers on an eight-bit signal. About 102 million slots belong to signals
wider than 64 bits, so a whole-signal single-word restriction would leave a
large part of this traffic untouched.

This suggests specializing masked composition and change detection using
known ownership. It does not prove that resolution can be deleted or that
these slots dominate CPU time. Raw driver values must remain distinct from
effective values affected by forces, deposits, or external publication.

[P33](performance-campaign.md#p33-raw-owned-span-projection-trial) is a useful
counterexample: its cache reached every unchanged-resolved slot in a reduced
diagnostic, but opposite-order cold pairs did not show a repeatable total
gain. High hit counts alone are insufficient grounds to retain an
optimization.

[P31](performance-campaign.md#p31-current-phase-and-update-attribution) reports
costs across native resume, cohort execution, boundary handling, update
matching, driver assignment, and notification. Inclusive call-path totals
overlap; they cannot be added to predict removable runtime.

## Proposed architecture

Keep time advancement, scheduling phases, dynamic processes, and external
callbacks under the generic scheduler. Invoke compiled execution units for
eligible static groups at their existing scheduling boundaries.

| Stage | Generated behavior | Intended reduction |
| --- | --- | --- |
| Specialized cohort dispatch | Fixed membership and bound state locations, readiness masks, direct native calls, static-wait completion handling | Repeated member comparisons, adapter preparation, and boundary decoding |
| Specialized updates and fanout | Fixed widths and ownership masks, bound driver locations, change detection, dependent-process readiness masks | Generic route classification, lookups, staging bookkeeping, and fanout traversal |
| Fused process regions | Eligible process bodies and internal propagation compiled together | Process-call overhead, intermediate materialization, and proven redundant scheduler round trips |

Use elaborated semantic/DesignIR and SimIR facts to construct a bounded
execution plan; avoid introducing another elaboration engine. Specialize the
topology around reusable process bodies. Bind instance-specific storage at
runtime so generated code can be shared where its semantic shape is equal.

Compile-time proofs should discharge immutable conditions. Mutable conditions
need runtime guards or reliable invalidation, preferably at group entry when
that is sufficient. A guard hoisted to entry is valid only if execution
cannot change the guarded condition before its last use.

Unsupported groups should retain the existing path. An unexpected boundary
inside a partially executed group must return an exact continuation: which
members completed, their state transitions, and pending updates. Falling back
must neither execute completed members twice nor lose their updates.

## Semantic obligations

Specialization must preserve:

- Simulation time, delta cycles, phase ordering, and deterministic member
  ordering. A dependency edge alone does not permit same-delta publication
  or removal of a scheduling boundary.
- Blocking and deferred update visibility, edge detection, repeated writes,
  and relevant transaction behavior even when an effective value is unchanged.
- Four-state and VHDL value semantics, raw driver state, resolution,
  strengths, force/release, deposits, and external drivers.
- Dynamic waits, suspension, fork-created processes or drivers, cancellation,
  stop requests, exceptions, and deferred native executor installation.
- Observation, tracing, coverage, foreign callbacks, and debugger boundaries
  required by the selected execution mode.

Readiness masks must preserve both duplicate suppression and legal repeated
activation. Feedback requires appropriate iteration and phase separation;
topological evaluation is sufficient only where the necessary acyclicity
and visibility conditions are proven.

Any restriction on observation or debugging must be an explicit capability
contract, not an accidental consequence of generated code. General
SystemVerilog/VHDL behavior must not silently become cycle-only simulation.

## Compilation cost and code sharing

The campaign measures cold end-to-end time, so added analysis and LLVM
compilation must be repaid within the measured run. Share repeated process
bodies, bound generated group size, and use selective inlining. A separate
monolithic LLVM module for every instance risks excessive compilation work
and instruction-cache pressure.

Persistent compiled-code identities would need to include the relevant
semantic topology, lowering mode, capabilities, ABI, and target configuration.
Runtime addresses belong in instance bindings, not portable cache identity.
Any lazy or background compilation policy must retain equivalent fallback
behavior and be assessed separately from the execution optimization itself.

## Recommended implementation sequence

1. Select one hot cohort family from P34 and establish actual readiness
   patterns, source eligibility, ordering, and boundary behavior. Recheck the
   current source after the concurrent optimization work settles.
2. Generate specialized dispatch through static-wait completion while
   preserving the existing update path. Reuse native process bodies and
   remove only work discharged by proof or guarded binding.
3. Validate partial readiness, unexpected exits, dynamic state changes, and
   continuation behavior against the current interpreter and native paths.
4. Measure fresh uninstrumented cold end-to-end comparisons. Retain the
   change only if the benefit survives compilation cost and transfers to the
   full workload; evaluate any separable simplification independently.
5. Specialize update and fanout handling for the same group, preserving raw
   driver and publication semantics. Coordinate this with the campaign's
   driver-representation work rather than creating competing state.
6. Consider fusion across process boundaries only after the preceding stages
   demonstrate a useful residual cost and provide a sound semantic foundation.

Correctness qualification should include interpreter/O0/O2 differential
execution, phase- and delta-sensitive witnesses, X/Z and relevant VHDL
values, wide disjoint writers, forces and external interactions, and
fallback after partial execution. Full campaign cases must retain identical
stimulus, transcripts, and final correctness summaries under the existing
parity rules.

Report compile, elaborate, native setup/simulation, total time, memory use,
and generated-code growth. Keep instrumented attribution separate from
speedup measurements. No numerical speedup estimate is justified by the
evidence reviewed here.
