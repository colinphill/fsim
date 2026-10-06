<!-- SPDX-License-Identifier: Apache-2.0 -->

# fsim engine v4: migration plan to a compiled static/event hybrid engine

Status: adopted, 2026-10-06 (owner decisions in §9). It replaces the
incremental A1–A7 sequence in
[simulation-performance-architecture.md](simulation-performance-architecture.md)
as the route to competitive performance.

## 1. Why the current architecture cannot get there

The current engine executes the elaborated design as a large set of
per-instance SimIR processes. Each process talks to a host runtime through
signals, drivers, publication, fanout and validation. Every optimization since
V7 has been layered *on top of* that model:

- horizontal fusion and cohorts (V7–V23);
- region graphs and frontiers;
- A4 planes;
- runtime cone fusion (the current working tree).

Each layer must be able to fall back to the per-process model at any
activation, so the per-activation host cost is the floor no layer can remove.

| Evidence (single fresh-cache runs, CPU 9) | Verilog DUT | Mixed (VHDL DUT) | Vivado xsim |
|---|---:|---:|---:|
| Total Wall | ≈32 s | ≈26 s | ≈16.9 s |
| Simulate phase (fsim: incl. ≈5–6 s setup + JIT) | ≈26 s | ≈22 s | 7.95 s |
| Simulate phase per clock cycle (44,922 cycles) | ≈580 µs | ≈490 µs | 176 µs |
| Activations per cycle | 314 | 403 | — |
| Elaboration | 5.9 s | 3.2 s | 8.15 s |

These figures are for the working tree with runtime cone fusion.

The [speed-of-light estimate](simulation-performance-architecture.md#23-speed-of-light-workload-reviewer-arithmetic-in-the-scratch-countpy)
puts the simulation work at 0.02–0.2 s. fsim's cost is therefore not
computation; it comes from four structural causes:

1. **The unit of execution is the process, not the design.** About 1 µs of
   host bookkeeping is spent per activation; a compiled-code simulator spends
   10–30 ns on a function call and a store.
2. **Code is generated per process, from per-op SimIR, at run time.** Lowering
   produces about 20 LLVM instructions per SimIR op. Optimizing that IR costs
   seconds (InstCombine alone takes 1.5 s on mixed), so the optimizer has to be
   kept weak.
3. **Elaboration lowers per instance** (≈84k lowering calls), and the results
   go through artifact encode/publish/decode on every run (≈2.3 s encode/publish
   plus ≈1 s decode on Verilog).
4. **The runtime surface is too large to keep fast:**
   - runtime ≈168k lines, application ≈116k, compiler ≈55k;
   - eight overlapping execution routes;
   - each new optimization adds another route rather than removing one.

Taking the existing plan to completion (A3/A4/J-D) would at best reach the
15 s target. It cannot reach industry-leading performance, because it keeps
both the process model and run-time per-process code generation.

## 2. What the fastest simulators do

These are the publicly documented techniques shared by commercial
compiled-code simulators (VCS, Xcelium, Questa with `vopt`) and by
cycle-based and research engines (Verilator, ESSENT, GSIM, RepCut):

| # | Principle | fsim today |
|---|---|---|
| P1 | Compile the whole design ahead of simulation, by module specialization; instances share code and own only data | per-process JIT, mostly per instance |
| P2 | Schedule synthesizable logic statically: levelize combinational logic, evaluate flops once per clock edge; keep event-driven execution for testbench, behavioral and timing code | every hop is a scheduled event |
| P3 | Activity gating: skip partitions whose inputs did not change | per-process sensitivity plus host fanout |
| P4 | Signals at fixed addresses; generated code loads and stores directly and detects changes inline | host callbacks, driver slots, publication |
| P5 | Values specialized by width and state: native integers ≤64 bits, dual-rail 4-state with a known-value fast path, optional 2-state mode | generic packed values, multi-copy |
| P6 | Optimize away what is not observed, controlled by explicit access levels (`+acc`, `-access`): net and port collapsing, constant propagation, dead logic | any observer disables optimization globally |
| P7 | Fast, cacheable, parallel compilation | weak cache reuse, serial |
| P8 | Multi-core partitioned evaluation for large designs | none |

v4 adopts P1–P7 as its core design and P8 as a later phase.

## 3. Target architecture

```
 frontends (kept) ──► elaboration (template-level, reworked)
                             │  SimIR process templates + instance tables (kept as the interface)
                             ▼
                    Design Graph IR (new)
         classification · net/port collapsing · constant folding ·
         levelization · clock recognition · activity partitioning · access plan
                             │
              ┌──────────────┴───────────────┐
              ▼                              ▼
      static RTL kernels             behavioral processes
  (edge kernels, levelized        (compiled resumable functions
   combinational partitions)        from SimIR)
              └──────────────┬───────────────┘
                             ▼
             AOT code generation (new; LLVM; per specialization; cached)
                             ▼
        v4 kernel runtime (new, small) + value library (kept) +
        observation layer (new, access-plan driven) +
        service adapters: VPI/VHPI/DPI, VCD/FST, Tcl/debugger, SystemC
```

### 3.1 Design Graph IR (DGIR)

The DGIR is a whole-design dataflow graph built from SimIR templates, the
signal and driver tables, and instance bindings. It has two levels:

- a per-specialization graph (shared code);
- an instance table (per-instance data offsets and constant bindings).

It contains:

- **Storage nodes.** Nets and variables, at per-element granularity for
  unpacked arrays (A1 semantics). Each carries a width, a state kind
  (2-state, Logic4 or Logic9), a resolution function and an access class.
- **Combinational nodes.** Pure SimIR bodies from continuous assigns, gates,
  port adapters, `always_comb`/`always @*`, and pure VHDL processes.
  Classification reuses the purity and ownership analyses already in
  `elaborated_design_cone_fusion.cpp` and `simir_region_graph.cpp`.
- **Sequential nodes.** Edge-triggered bodies matched by generic structural
  rules:
  - single edge plus optional asynchronous reset;
  - nonblocking or VHDL signal writes only;
  - no waits or delays.
- **Behavioral islands.** Everything else: delays, dynamic waits, fork, tasks,
  classes, system tasks with side effects, and unsupported constructs.

Analyses on the DGIR:

- alias and port collapsing;
- constant propagation across instance bindings (for example, constant
  multiplier operands);
- dead-logic removal subject to the access plan;
- levelization, with cycle detection: combinational loops and latches become
  behavioral;
- clock-tree recognition, including derived and gated clocks;
- activity partitioning into supernodes.

Classification is purely structural. It must never key on design, module or
path names.

### 3.2 Execution model and semantics

A static kernel is a pseudo-process that runs in the same scheduling regions
as the processes it replaces. Only internal hops that cannot be observed are
collapsed.

- **Verilog/SV.** A combinational partition evaluates in topological order
  within one Active pass; IEEE 1800 §4.7 allows any Active order. An edge
  kernel evaluates its flops in Active and commits them in NBA, exactly as the
  original `always_ff` processes would. This is the IEEE-aligned default
  already adopted in §7 of the current architecture document.
- **VHDL.** IEEE 1076 requires a delta per zero-delay signal update. Each DGIR
  storage node has a static *delta depth* relative to its trigger. Signals read
  only by pure nodes are evaluated without materializing deltas. Signals
  observed by impure readers (testbench processes, `'event`/`'transaction`
  users, postponed processes) are published at their exact reference delta.
  This keeps VHDL cycle-exact at every observable boundary while removing the
  per-hop process overhead.
- **Mixed boundaries.** Each crossing uses the existing cross-language lattice
  and is published at its reference delta.
- **Standard conformance.** Every collapse is an as-if transformation: it is
  admitted only when no construct the standard defines can distinguish it from
  the reference schedule. If conformance cannot be proved, the exact schedule
  is kept (§9, D2).
- **Races.** Orderings the standard leaves undefined, such as testbench
  blocking writes at the clock edge, need not match any particular simulator.
  v4 uses one documented deterministic order. The corpus runs race-free,
  deterministic testbenches, so parity is judged only on defined behavior
  (§9, D4).

### 3.3 Activity gating and partitioning

- **Supernodes.** Combinational logic is grouped into partitions of roughly
  20–200 operations, one per fanout-closed group, ESSENT/GSIM style.
- **Dirty bits.** Each partition has a dirty bit, set by inline change
  detection on its input stores. Each clock edge sets a dirty bit for its edge
  kernel.
- **Kernel loop.** A partition runs only when it is dirty, in level order.
  The whole loop is generated code; there is no host dispatch per partition.
- **Behavioral readers.** Fanout to behavioral processes uses static fanout
  arrays and an intrusive ready queue.

### 3.4 Values

- **Narrow values.** Widths ≤64 bits use native integers. 4-state values are
  dual-rail (`aval`/`bval`), with a branch-predicted known-value fast path.
- **Wide values.** Wider values use inline word arrays; there is no
  copy-on-write on the hot path.
- **Logic9.** Logic9 stays internal to VHDL `std_logic` resolution. Unresolved
  or single-driver `std_logic` nets that never carry strengths map to Logic4
  when that mapping is provably exact.
- **2-state mode.** An optional opt-in mode (`--two-state`, P5) is a later
  item. It is never applied silently.
- **Reuse.** The `packed_value` library and the SimIR operation semantics
  remain the reference implementation and the behavioral fallback.

### 3.5 v4 kernel runtime

- **Storage.** One contiguous signal arena. Offsets are fixed at compile time,
  and generated code addresses signals directly.
- **Scheduler.**
  - Regions: Active, Inactive, NBA/update, postponed and the VHDL cycle.
  - Future events use a timing wheel.
  - NBA and VHDL transactions are compact per-delta arrays.
- **Behavioral processes.** These are compiled resumable functions with a
  state index and a frame. Waiting and resuming each cost one store and one
  queue push. There is no per-activation validation or runtime-struct rebuild.
- **Language services.** Classes, dynamic arrays, queues, strings, file I/O,
  randomization and DPI are runtime-library calls that reuse the existing
  implementations where their interfaces allow.
- **Size.** Target ≤25k lines for the core kernel. The old runtime is retired
  rather than adapted.

### 3.6 Observation and access

Observability is decided at compile time from an access plan:

- the default `simulate` run;
- trace selection;
- VPI/VHPI/Tcl access flags;
- `force`/`deposit` targets;
- coverage options.

| Access class | Storage | Cost |
|---|---|---|
| Internal pure net, unobserved | none (register in kernel) | zero |
| Readable on demand (default for named nets) | none; materialized by recomputing its cone at a quiescent point, as in the current runtime cone fusion | paid only on read |
| Value-change observed (trace, VPI callback, `$monitor`) | arena slot plus an inline change record | that signal only |
| Writable (force/deposit/VPI put) | arena slot plus a force mask; the partition reads through the mask | that partition only |

A request outside the compiled access plan, such as a debugger forcing an
optimized net, recompiles the affected partition as behavioral at the next
safe point. It does not disable the whole engine.

### 3.7 Compilation pipeline and time budget

- **IR generation.** LLVM IR is emitted from the DGIR per specialization, not
  per SimIR op per process. Instance data is reached through a base pointer,
  so the 540 multiplier instances share at most a few functions.
- **Optimization.** A short, tuned pipeline (O1-class plus SROA/GVN on bounded
  functions) with function size caps. ORC is used only to load, not to drive
  compilation.
- **Caching.** Objects are cached by content hash (ABI, semantics, target, and
  DGIR fingerprint) and compiled in parallel when cores are available.
  Qualification uses one core and fresh caches, so the single-thread budget
  below must hold without them.
- **Elaboration.** Elaboration works at template level (A5): each
  specialization is lowered once and instances become binding rows.
- **Artifacts.** The artifact format is a flat, mmap-ready image. A one-shot
  run never encodes, publishes and decodes on its critical path.

## 4. Performance budget

Single core, fresh cache; values in seconds.

| Phase | Verilog now | Mixed now | v4 target (each case) |
|---|---:|---:|---:|
| Parse + semantic (fsim "compile") | 0.53 | 1.14 | ≤1.0 |
| Elaboration (incl. artifact encode/publish) | 5.9 | 3.2 | ≤1.0 |
| Setup before time 0 (artifact load, JIT, runtime setup; from 1 ns runs) | ≈6.3 | ≈5.4 | ≤1.7 (DGIR + codegen ≤1.5, startup ≤0.2) |
| Simulation loop (remainder) | ≈19–20 | ≈16–17 | ≤2.0 (≤45 µs/cycle) |
| **Total** | **≈32** | **≈26** | **≤6** |

The "now" figures come from single observations, so they carry the ±5%
noise noted in the campaign documents.

The simulation budget leaves 10–100× headroom over the speed-of-light
estimate. It is roughly 4× faster per cycle than xsim, which is the level
needed to claim a lead rather than parity.

## 5. Kept, replaced, retired

| Area | Disposition |
|---|---|
| Frontends, semantic analysis, HIR | **kept** |
| Elaboration, hierarchy, specialization | **reworked** to template-level lowering; per-instance work becomes binding rows |
| SimIR, process templates | **kept** as the engine input and as the behavioral-process source |
| Value library (`packed_value`, logic operations) | **kept** as reference semantics and runtime library |
| Interpreter (`simir_interpreter`, `simir_execution`) | **kept, frozen** as the differential oracle; it stops receiving performance work |
| Runtime scheduling, publication, drivers, fusion, cohorts, regions, frontiers, A4 planes, native frontier (most of `src/runtime`) | **replaced** by the v4 kernel; retired after the switch |
| `src/compiler` per-process JIT and region-frontier codegen | **replaced** by DGIR codegen; value intrinsics and LLVM plumbing reused where they fit |
| `src/app` executors, simulation setup | **replaced** by a thin v4 driver; project, CLI and service plumbing kept |
| VPI/VHPI/DPI, VCD/FST, Tcl/debugger, coverage, SDF, SystemC | **re-hosted** through adapters onto the v4 observation layer |

## 6. Migration strategy

- **Side by side.** v4 runs as `--engine v4` next to the current engines. The
  current engine stays the default until v4 passes the full suite.
- **Whole-design admission.** v4 either accepts a design or rejects it with
  explicit reason codes; there is no per-process mixing with the old runtime,
  which is the main source of today's complexity. Rejected designs run on the
  current engine. Admission coverage of the test corpus is a tracked metric.
- **Differential gating.** Every v4 run in CI is compared with the frozen
  interpreter on transcripts, settled values at each time step, and
  value-change streams for observed signals. External oracles add xsim
  (outside the sandbox) for 4-state and VHDL, and a 2-state cross-check for
  synthesizable designs.
- **Freeze.** Performance work on the current runtime stops (§9, D1). Fixes
  continue only for correctness.
- **Uncommitted work.** The working-tree cone-fusion work carries over. Its
  classifier, internal-net eligibility rules, lazy materialization and test
  (`tests/app/cone_fusion_test.cpp`) become DGIR building blocks and witness
  tests.

## 7. Phases

Effort ranges are in engineer-weeks and are rough. They assume the current
mode of working: one owner plus coding agents.

### Phase 0: contract, harness, corpus (2–3 wk)

- **Semantic contract.** Write `docs/engine-v4-semantics.md`, covering
  levelized Active evaluation, VHDL delta-depth publication, the race policy,
  access classes and on-demand materialization.
- **Differential harness.** Build the interpreter-versus-v4 harness, keyed by
  time step plus observable delta.
- **Benchmark corpus.** The corpus goes beyond the two throughput cases:
  - the ten existing campaign cases, run through their deterministic fixture
    generators (`scripts/perf_throughput_fixture.py`,
    `perf_codec_fixture.py`, `perf_codex_fixture.py` and
    `perf_codex_throughput_fixture.py`), never the raw `$urandom` testbenches;
  - `ldpc_codex`;
  - several open-source RTL cores (CPU, crypto, and an SoC slice) with
    license-reviewed testbenches;
  - one UVM-style testbench.
- **Comparison table.** Record xsim times, and a 2-state reference where
  licensing allows.
- **Exit criteria.** The contract is approved, the harness runs on the
  current engine, and corpus baselines are recorded.

### Phase 1: Verilog vertical slice (6–8 wk), the go/no-go gate

- **DGIR.** Build the DGIR for the synthesizable Verilog subset, with
  classification, levelization, collapsing, constant folding and partitions.
- **Kernel and codegen.** Implement a minimal v4 kernel, plus codegen for
  static kernels and for the behavioral subset the throughput testbench uses:
  `initial`, delays, waits, fork, `$display`, file I/O and tasks.
- **Exit criteria.**
  - `original_throughput` Verilog runs to completion with exact transcript
    parity against the frozen interpreter;
  - simulation ≤3 s;
  - total ≤10 s on one core.
- **On a miss.** Stop and re-plan before investing in breadth. This phase
  exists to test the central performance thesis cheaply.

### Phase 2: VHDL and mixed (4–6 wk)

- **Scope.** Logic9 and resolution functions, VHDL processes, delta-depth
  publication, and mixed-language boundaries.
- **Exit criteria.**
  - mixed throughput parity;
  - Verilog within 5% of mixed;
  - `rs-vhdl` parity.

### Phase 3: front-end time (4–6 wk, overlaps Phase 2)

- **Scope.** Template-level elaboration, the flat artifact image, and the
  codegen time budget.
- **Exit criteria.** Both throughput cases at median total Wall ≤15 s under
  the paired qualification protocol. This meets the current contractual
  target, then heads toward the ≤6 s budget in §4.

### Phase 4: breadth (12–24 wk, the largest phase)

- **Features, in order of corpus impact:**
  - functions and tasks, dynamic data, strings;
  - classes, randomization, UVM;
  - DPI;
  - VPI and VHPI;
  - force/release and deposit;
  - assertions and functional/code coverage;
  - VCD/FST tracing;
  - Tcl and the debugger;
  - SDF and specify timing;
  - SystemC;
  - PSL.
- **Exit criteria per feature.** The feature's existing gates pass under
  `--engine v4`, and corpus admission rises.

### Phase 5: default switch and retirement (4–8 wk)

- **Switch.** v4 becomes the default once the full suite passes and the corpus
  is admitted.
- **Retirement.** Remove the replaced runtime, compiler and app layers, and
  shrink the native-cache schema to the v4 ABI.
- **Kept.** The interpreter remains as the oracle and as the `debug` engine.

### Phase 6: lead extensions (ongoing)

- multi-core partitioned simulation (RepCut-style replication);
- incremental compilation per changed specialization;
- opt-in 2-state mode;
- profile-guided partition layout;
- save/restore.

| Milestone | Cumulative estimate |
|---|---|
| Go/no-go result (end of Phase 1) | ≈2–3 months |
| Contractual 15 s target on v4 (Phase 3) | ≈3.5–5 months |
| v4 default, old engine retired | ≈8–14 months |

## 8. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Semantic divergence (races, glitches, observable deltas) | conservative structural classification; VHDL delta-depth publication; frozen interpreter oracle on every run; witness tests per contract clause |
| The performance thesis fails on real designs | Phase 1 go/no-go on a full run before breadth; a corpus beyond the RS benchmark |
| Compile time grows on large designs | per-specialization code, size caps, a bounded pass pipeline, object cache, parallel compile |
| Feature breadth takes longer than planned | whole-design fallback to the current engine; no forced cut-over |
| Two engines maintained at once | freeze the old runtime's performance work; Phase 5 retirement is part of the plan, not optional |
| Observability expectations (debugger, VPI) | explicit access plan plus on-demand materialization and per-partition recompile, never a global cliff |

## 9. Owner decisions (resolved 2026-10-06)

- **D1. Adopted.** Performance work on the current runtime stops: the
  R-series and the remaining A3/A4/J-D items. The working-tree cone fusion and
  R-series state are committed as a checkpoint.
- **D2. Adopted, provided the result never violates the standard.**
  - Verilog: levelized Active evaluation.
  - VHDL: internal-hop collapsing with exact delta-depth publication at every
    observable boundary, admitted only as an as-if transformation.
- **D3. Adopted as proposed.** By default named nets are readable on demand.
  Value-change and write access are compiled in only when requested by trace
  selection, VPI/VHPI, Tcl, force/deposit or coverage options.
- **D4. Adopted.** Undefined races do not need to match any simulator, and
  testbenches are refactored to avoid them; transcript parity is then required
  against every oracle.
  - For the existing campaign cases this refactoring is already done. Their
    deterministic fixture generators produce race-free testbenches:
    - stimulus is driven on the negative edge;
    - signals are sampled at the positive edge;
    - a testbench-defined xorshift32 replaces `$urandom`.
  - **Check on 2026-10-06.** The fixtures from
    `build/performance-campaign/matrix-closure-20260928/round-2` were run on the
    checkpoint build. Both throughput cases reproduced all 13 Vivado result
    lines exactly (`THRU` and `STIM_SUMMARY`).
  - **Superseded concern.** The apparent Vivado mismatch came from comparing
    runs of the raw `$urandom` testbenches against fixture-based Vivado
    results. It was not a parity defect.
- **D5. Adopted.** Whole-design admission, with fallback to the current engine
  during migration.
- **D6. Adopted.** "Industry-leading" means:
  - on the corpus, at least 3× faster than xsim in total Wall, single-core;
  - simulation time within 2× of a 2-state cycle-based reference on
    synthesizable designs, while remaining 4-state exact.
