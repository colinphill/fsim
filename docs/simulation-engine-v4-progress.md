<!-- SPDX-License-Identifier: Apache-2.0 -->

# fsim engine v4: progress log

This is a running record of the migration in
[simulation-engine-v4-plan.md](simulation-engine-v4-plan.md): what has been built, how it maps
to the plan's phases, the latest measurements, and what comes next. The newest entries are at
the top of §4. Nothing here is committed yet unless a commit is named.

## 1. Targets

| Goal | Verilog (`original_throughput`) | Mixed (`mixed_throughput`) |
|---|---:|---:|
| Contract | ≤15 s median total Wall, with Verilog ≤1.05× mixed | ≤15 s |
| D6 "industry-leading" (≥3× faster than xsim) | ≤ about 5.6 s total | ≤ about 4.6 s total |
| Plan budget (§4) | ≤6 s total, ≤2 s simulation loop | the same |

Every target also requires exact Vivado parity on the deterministic round-2 fixtures (13
`THRU`/`STIM_SUMMARY` lines) and HDL report parity with the reference engine.

## 2. Current status (2026-10-06, late)

Times are fresh-workspace runs on CPU 9 in seconds, kernel on (`FSIM_STATIC_KERNEL=1`),
without profiling. `FSIM_PROFILE_KERNEL` adds about 25% to the run phase and
`FSIM_PROFILE_PHASES` inflates elaboration, so neither is used for timing. Repeated runs
delete `.fsim/cache/llvm-native` first: a warm native cache for the testbench saves about
0.9 s and is not allowed under the fresh-cache protocol (`r37-analysis/tools/rep.py` does
this). Wall times on this VM drift by 5–10% over hours, so small changes are judged by
instruction counts (`perf stat -e instructions:u`).

| Phase | Verilog: HEAD | Verilog: v4 | Mixed: HEAD | Mixed: v4 |
|---|---:|---:|---:|---:|
| Compile | 0.87 | 0.89 | 1.54 | 1.6–1.7 |
| Elaborate | 5.6 | **3.7** | 3.1 | **3.1** |
| Simulate (wall) | ≈11.5 | **≈9.6** | 21.5 | **≈8.9–9.5** |
| **Total** | **≈18** | **≈14.2** | **≈26** | **≈13.6–14.4** |

Elaboration on Verilog went from 55.8G instructions (HEAD) to 29.6G, with the snapshot
byte-identical.

**Test suite.** 37 tests fail on the committed HEAD build as well. They are the
same-named failures, built from the exported HEAD tree with tests, and include the
scheduling witnesses, the native region route tests, the closure audits and
`windows-llvm-contract`. The one failure new to the working tree,
`fsim.native-path-io` (the kernel perf map bypassed `support::native_fs`), is fixed.

### Phase exit criteria

A phase is committed when it meets the plan's exit criteria (owner, 2026-10-06: one commit
per completed phase).

| Phase | Exit criteria (plan §7) | Status |
|---|---|---|
| 0 Contract, harness, corpus | contract approved, harness running, baselines recorded | partial: the `FSIM_KERNEL_VERIFY` replay harness and parity scripts exist; contract doc and corpus not yet |
| 1 Verilog slice | exact parity, simulation ≤3 s, total ≤10 s | parity met; simulation ≈9.8 s and total ≈14.7 s not met |
| 2 VHDL and mixed | mixed parity, Verilog within 5% of mixed, `rs-vhdl` parity | **met** on 2026-10-06 (paired run below): mixed parity; `rs-vhdl` parity on every testbench that elaborates (5 of 8; the other 3 fail elaboration on HEAD too); Verilog/mixed 1.005 |
| 3 Front-end time | both throughput cases ≤15 s median (paired protocol) | **met** on 2026-10-06 (paired run below): Verilog 13.47 s, mixed 12.80 s |

## 3. Mapping to the plan

| Plan item | State | Where |
|---|---|---|
| §3.2 Verilog: levelized Active evaluation, NBA commit | done | `src/runtime/simir_static_kernel.cpp` |
| §3.2 VHDL: exact deltas | done (one kernel round = one delta, and one host delta at mixed boundaries) | same, `vhdl_` mode |
| §3.2 VHDL: delta-depth collapsing, as-if | not started | — |
| §3.3 Supernodes/partitions, dirty bits | Verilog only: per-instance partitions, level buckets | `build_partitions` |
| §3.4 Narrow native values, 4-state dual-rail | done (KIR) | `simir_static_kernel_ir.hpp`, `simir_kernel_word_ops.hpp` |
| §3.4 Logic9 internal to `std_logic` | done: dual-rail words, 'U' shadow masks, and deoptimization for W/L/H/'-' | `simir_static_kernel_compiled.cpp` |
| §3.4 Wide values without copy-on-write | partial: field reads from wide slots; wide registers still go through the reference evaluator | |
| §3.5 Contiguous signal arena | done (kernel-owned signals) | kernel `arena_` |
| §3.6 Hidden nets readable on demand (D3) | done (`materialize`) | `prepare_signal_observation` hook |
| §3.7 Generated code per canonical template, not per process | done (LLVM, about 740 templates for mixed) | `src/compiler/static_kernel_codegen.cpp` |
| Phase 0 harness: frozen-oracle comparison | partly done: `FSIM_KERNEL_VERIFY` replays every compiled VHDL run on the reference evaluator | §4 |
| D5 whole-design fallback | done (planner disables the kernel with a reason) | `elaborated_design_static_kernel.cpp` |

## 4. Log

### 2026-10-06 (night): Phase 2 qualified (paired protocol)

The command is the Phase 3 one, with the frozen candidate `v4cand-phase2-10061746`.
Evidence: `.local-artifacts/simulation-performance/v4-phase2-qualification-10061753`.

| Case | Control (HEAD) median | Candidate median | Ratio |
|---|---:|---:|---:|
| `original_throughput` | 30.88 s | **12.82 s** (compile 0.59, elaborate 3.45, simulate 8.77) | 0.414 |
| `mixed_throughput` | 25.25 s | **12.76 s** (compile 1.14, elaborate 3.10, simulate 8.52) | 0.503 |

- Verilog/mixed is 1.005 (limit 1.05).
- The runner's combined absolute verdict passes, and so does its anti-slowdown guard
  (limit 13.39 s).
- Both preflights matched Vivado.
- **Phase 2 exit: met** (mixed parity, `rs-vhdl` parity, Verilog within 5% of mixed).

### 2026-10-06 (night): `rs-vhdl` parity, mixed delta boundaries, element reads

**`rs-vhdl` parity (Phase 2 criterion).**
- Method: `r37-analysis/tools/rs_vhdl_parity.py`. Each SystemVerilog testbench in
  `~/vprojects/rs-vhdl/tb` drives the VHDL RTL, compiled in `sim/run_vhdl_sim.tcl` order as
  VHDL-2008. Each testbench is simulated with and without the kernel (same seed, compiled
  engine, fresh native cache).
- Summary: `.local-artifacts/simulation-performance/rs-vhdl-parity/summary.txt`.

| Testbench | Functional transcript | PASS/FAIL | Reference sim | Kernel sim |
|---|---|---:|---:|---:|
| `rs_codec_tb` | identical | 75/0 | 39.3 s | 15.2 s |
| `rs_corrbits_tb` | identical | 9/0 | 5.8 s | 2.8 s |
| `rs_param_tb` | identical | 336/0 | 250.8 s | 83.3 s |
| `rs_parlen_tb` | identical | 8/0 | 6.0 s | 2.8 s |
| `rs_thru_tb` | identical | — | 27.2 s | 8.3 s |
| `rs_decoder_tb`, `rs_encoder_tb`, `rs_symsize_tb` | do not elaborate, on HEAD (e8ecb67e) too | | | |

- **Elaboration gaps.** These are front-end gaps, outside the engine:
  - SystemVerilog `fork` around task calls with array arguments;
  - VHDL-2008 `maximum` in a generate-scoped constant;
  - a slice assignment inside a for-generate.
- **Time-0 metavalue warning counts.** Every other line matches, but one location's
  `to_integer` metavalue warnings come out once more per decoder instance in the kernel
  (`in_ready`, `rs_decoder_top.vhd:263/265`, all at time 0). Example: rs_param_tb, 40
  instances, reference 136, kernel 176.
  - Cause: the testbench's `initial` block (`s_tlast = 0` …) and VHDL initialization
    both run in delta 0.
  - The reference runs the decoder's explicit processes, then the testbench, then the
    concurrent statements, so `in_last` first evaluates with the testbench value. The
    kernel runs all VHDL initialization together and sees X first, which costs one more
    `in_ready` evaluation while `wr_bank` is 'U'.
  - Neither IEEE 1076 nor IEEE 1800 orders VHDL initialization against SystemVerilog
    `initial` blocks, so this is an undefined race under D4. It is documented in §5.

**Mixed boundaries at their reference delta (plan §3.2).**
- `activate()` used to run every VHDL round of one activation inside a single host delta.
  Host processes therefore saw only the last value of each kernel output and could drive
  kernel inputs only after the kernel had settled.
- Now each kernel round that leaves more work publishes and yields to the host's next
  delta, so one kernel round is exactly one host delta.
- Witness: `tests/app/static_kernel_vhdl_test.cpp` gained a one-delta glitch (`pulse`)
  that the testbench counts with `always @(pulse)` and feeds back into a VHDL
  `rising_edge` counter. With the yield removed, the test fails (`pulses=2` instead of 4).
- Cost on `mixed_throughput`: about 3% more simulation instructions (151.5G to 156.3G) and
  about 0.3 s of simulation time (8.7 to 9.1 s). Kernel activations rise to 311k, but only
  21k publish anything.
  - A conditional yield would recover most of this: yield only when an output changed or
    the host has work left in the time slot. That needs a scheduler query for "other
    pending work in this time slot", which is listed in §6.
  - Packing the input scan into flat arrays did not change the instruction count, so it
    was reverted.
- Mixed result lines and HDL reports (17,018) are unchanged.

**Element reads of kernel-owned memories from host code.**
- Testbench JIT reads of a memory go through `container_read_packed_impl`. In the
  borrowable case it borrowed the whole object, and `read_container_object_value`
  rebuilt every element from its element-signal aliases on each read. Kernel memories are
  element slots, so they have such aliases. Each single-element read therefore cost a
  pass over the memory: 2.7M reads on Verilog.
- The borrowable case now reads the selected element with
  `read_container_object_element` and falls back to the borrow only if that declines.
- `read_container_object_element` gained the exposed-reference rule of
  `read_container_object_value`, so both agree.
- Verilog simulation: 85.9G to 83.7G instructions (−2.6%), transcript byte-identical.
- `fused_object_borrow_reads` still counts every borrowable read at the point where it is
  classified, as `fsim.application.wide-file-binary-read-jit` expects.

**Checks on the final binary** (the Phase 2 candidate):
- Full test suite: the same 37 failures as the HEAD control build (e8ecb67e), and nothing
  new. `fsim.deterministic-packaging` failed once under `-j10` load and passes on rerun.
- `rs-vhdl`, all five elaborating testbenches: functional transcripts identical, with the
  same time-0 warning-count race as above.
  Summary: `.local-artifacts/simulation-performance/rs-vhdl-parity/summary-final.txt`.
- Mixed: result lines match, HDL reports 17,018 with identical per-location counts.
- Frozen candidate: `.local-artifacts/v4cand-phase2-10061746`, made by
  `r37-analysis/tools/freeze_candidate.sh`.

**Where Verilog simulation time goes** (time-based samples, about 9.9 s):

| Window | Activity |
|---|---|
| 0–2.0 s | load the artifact: runtime-state decode, `from_state` region graph, `create_interpreter`, `StaticKernel` construction, `plan_static_kernel` |
| 2.0–4.0 s | LLVM code generation for 384 kernel templates |
| 4.0–5.0 s | testbench start: the memory reads fixed above, `RegionGraphProgramBuilder` |
| 5.0–9.9 s | the run; about 55% of samples are in generated kernel code |

- `--duration N` runs take 23.7 s even at 1 ps, against 10 s for the full run to
  `$finish`. The time-limit exit path does something expensive. The benchmarks end in
  `$finish`, so this is noted in §5 rather than chased.

### 2026-10-06: Phase 3 qualified (paired protocol)

Command:

```sh
FSIM_STATIC_KERNEL=1 setarch x86_64 -R python3 scripts/perf_pair_campaign.py \
  --control <HEAD e8ecb67e release build>/fsim \
  --candidate <frozen copy of the working-tree build>/fsim \
  --pairs 7 --seed 0x6d2b79f5 --cpu 9
```

Run conditions:
- Fresh native caches per leg, one core.
- The control and candidate preflight transcripts both matched Vivado.
- Evidence is in `.local-artifacts/simulation-performance/v4-phase3-qualification-10061648`.
- `setarch -R` (ASLR off) is needed because the runner's identity check compares raw
  `ldd` output, whose load addresses ASLR randomizes. With ASLR on, the preflight
  identities never match.

| Case | Control (HEAD) median | Candidate median | Ratio |
|---|---:|---:|---:|
| `original_throughput` | 32.32 s | **13.47 s** (compile 0.64, elaborate 3.55, simulate 9.27) | 0.415 |
| `mixed_throughput` | 26.25 s | **12.80 s** (compile 1.18, elaborate 3.05, simulate 8.52) | 0.488 |

- **Phase 3 exit (both cases ≤15 s median): met.**
- The runner's combined absolute verdict also requires Verilog ≤1.05× mixed, and an
  anti-slowdown guard of 13.44 s. Verilog/mixed was 1.052 and Verilog 13.47 s, so both
  missed by about 0.03 s. That is a Phase 2 criterion and remains open.

### 2026-10-06 (evening): wide moves, helper traffic, partition components

- **Wide moves.** A `ReadSignal` of a wide slot whose register only feeds an optional
  `DynamicPartSelect` and one write to a wide slot now runs as one word-level
  `KOp::wide_move`, fused at compile time in straight-line code that does not write the
  source. Nonblocking moves queue one word write per 64 bits. Generic-bridge calls on
  Verilog went from 2.65M to 0.17M, and profiled kernel activation from 4.9 s to 4.2 s.
- **Mixed helper traffic.**
  - Narrow dynamic part-selects from wide Logic9 or Logic4 slots read inline when the
    selection maps result bit i to source bit first + i.
  - The slow path for wide sources reads the field in one piece instead of bit by bit.
  - 32-bit VHDL `/`, `rem` and `mod` are inline.
  - Binary operators the generator does not inline (`**`, divide) call a pure word
    helper instead of the general evaluate path.

  Mixed evaluate calls went from 9.3M to 3.6M, and profiled activation from 7.4 s to
  6.4 s.
- **Partitions split into connected components.** A module instance's combinational
  members form one partition per weakly connected component, so a change runs only the
  cone it reaches. Verilog kernel operations went from 3.08G to 2.74G and the run phase
  dropped by about 0.4 s. `FSIM_STATIC_KERNEL_WHOLE_PARTITIONS` restores one partition
  per instance.
- **Wide host mirrors and in-place reads of written slots.**
  - Host signals wider than 64 bits that members read get multiword mirror slots, so
    narrow selections read them in place (ROM contents in mixed).
  - A slot the member itself writes may still be read in place when no write to it, no
    backward jump and no call lies between the read and its last selection. This is the
    read-before-write of a RAM process.
  - Mixed generic calls went from 1.87M to 0.53M, and profiled activation from 6.2 s to
    5.2 s.
  - HDL reports (17,018 at 11 locations) and result lines still match the reference
    engine exactly, and a 5000 ns `FSIM_KERNEL_VERIFY` replay found no mismatch.
- **Constant memo.** 87% of SystemVerilog constant evaluations repeat a (specialization,
  expression) pair because instances share specializations. Results are memoized on the
  specialization (`SpecializedHirUnit::client_cache`, detached by `replace`). Elaboration
  went from 36.9G to 33.6G instructions; the snapshot is byte-identical.
- **More elaboration cuts** (snapshot byte-identical each time):
  - word-level `known` and unsigned conversion of constants: 33.6G to 31.3G instructions;
  - overlay equivalence classes computed once per specialization replace overlay
    comparisons in the template caches: 31.3G to 30.5G;
  - the path codec sorts only the unsorted remainder of each table and merges: 30.5G
    to 29.6G.

  That is 47% below HEAD's 55.8G.
- **Elaboration.** Process canonicalization made about 25 share attempts per process.
  Remembering the representative each template last shared with cut elaboration from
  41.2G to 36.9G instructions (HEAD: 55.8G); the snapshot is byte-identical.
- **Build.** `simir_static_kernel_compiled.cpp` (4.6k lines) is split into compile,
  analysis, native-unit and execution units with a shared internal header.
- **Tried and reverted (measured on Verilog).**

  | Change | Codegen | Run | Verdict |
  |---|---|---|---|
  | Codegen level 1 for every template | +2.45 s | −1.5 s | net loss |
  | Level 1 for statically hot templates (instances × size, various thresholds) | — | — | net loss at every threshold |
  | Tiered recompilation of the templates with the most measured work | — | — | gain too small; the benefit is spread over all templates |
  | Registers of a run kept in SSA (liveness-based) instead of the register file | — | none | no gain at O0 |
  | Per-member dirty bits inside partitions (plan §3.3) | — | — | slower, see below |

  Dirty bits skipped 38% of member runs on the interpreter tier. The run still got
  slower, for two reasons:
  - member boundaries become basic blocks, which costs about 0.8 s at O0;
  - the ranged-reader shortcut "partition already queued" no longer applies, so
    `slot_changed` grew to 13.6% of the run.
- **Correctness bookkeeping.**
  - `fsim.windows-llvm-contract` fails on the committed tree: the CMake check expects
    680 while the test sources say 688.
  - `fsim.application.systemverilog-scheduling-witnesses.llvm-O0/O2` fails the same way
    with the committed HEAD build (`region_backend_runs=0`).

  Neither failure comes from this work.

### 2026-10-06 (later): path visits, shared region graph, inline memories

- **Path visits test the operation kind first.** Hierarchy-path collection, the runtime path
  codec and `freeze_hierarchy_paths` expanded every operation of every process (instance
  fields applied, operation copied) just to find the few coverage and debug operations
  that carry paths. They now test the stored kind first. Verilog elaborate went from 4.65 s
  to 4.33 s and simulate from 11.10 s to 10.49 s (medians of 3; snapshot byte-identical).
- **One region graph per loaded design.** The loader's driver inventory and
  `plan_static_kernel` built the same region graph. `ElaboratedDesignProcessAccess::
  region_graph` caches it on the immutable row table, keyed by a hash of the signal,
  memory and alias layout.
- **Inline memory access (SystemVerilog kernel).** Kernel memories are element slots or
  one packed slot. `StaticKernelContainerInfo` gives generated code their shape:
  - reads are inline loads;
  - nonblocking element writes are appended to the ordered queue as ordinary word writes
    to the element's slot, which notify readers exactly as the reference element write
    does;
  - blocking, part-select and two-state-with-X writes and VHDL memories still use the
    helpers.

  This removed 3.3M evaluate and 1.4M effect helper calls on Verilog. Kernel activation
  went from 5.8 s to 4.9 s (profiled).
- **Inline narrow stores into wide slots** (with a field-offset notify helper) removed
  another 1.1M effect calls.
- **Measured.**
  - Notifications are now spread over about 13k slots, with no dominant pattern.
  - The largest remaining kernel item on Verilog is wide (more than 64-bit) values
    through the generic bridge: 2.65M calls.
  - Host-JIT compile of the testbench (about 0.9 s CPU) is now partly on the critical
    path: the run waits up to 0.6 s at the background-compile barrier.

### 2026-10-06: Phase 3 front-end and setup cuts

All elaboration changes keep the snapshot byte-identical (`r37-analysis/tools/elab_hash.sh`
compares SHA-256 of every payload file against the baseline).

- **Generate occurrences are cached per instance template.** `collect_occurrences`
  specialized every genvar value of every instance again. Occurrences now hold shared
  specializations, and `HierarchyBuilder::collect_generate_occurrences` reuses them for
  instances whose overlay is template-equivalent (`same_systemverilog_template_overlay`)
  and whose specialization has no replacement records. Verilog elaboration went from
  5.7 s to 4.7 s.
- **Hierarchy path table.** The table kept a `std::deque<std::string>` plus an
  `unordered_map`. It now uses a chunked character arena and an open-addressing index, and
  extending a table shares the source arena instead of re-interning. Interning, lookup and
  teardown were about 14% of elaboration. Verilog elaborate went from 5.09 s to 4.65 s
  (median of 3), simulate from 11.33 s to 11.10 s.
- **No teardown at exit.** The `fsim` executable sets
  `support::enable_exit_without_teardown()`. The interpreter state and the elaborated
  project are then kept until exit instead of freed; HDL files are flushed first and
  destructors with side effects still run. Library and test callers are unaffected.
- **Kernel notification.**
  - Ranged readers are grouped by the target they would schedule, and a group whose
    target is already queued is skipped. This removed about 31M redundant schedule calls.
  - Slots without readers, edges or publication are silent, so stores to them do not
    call the runtime. Native notify calls went from 32.1M to 17.5M.
  - A dense member-to-process table keeps writer bookkeeping off the 608-byte member
    records, and single-writer slots skip it entirely.
- **Codegen pipeline.** Cold templates run only SROA before the fast instruction selector;
  hot templates keep `sroa,early-cse,simplifycfg`. Static-kernel codegen on Verilog went
  from 1.19 s to 0.87 s. The run phase did not change measurably on either design.
- **Measured but not changed.**
  - The testbench's host processes need the native tier: running them in the interpreter
    makes the Verilog run 29 s. Their LLVM compile is about 0.9 s on a fresh cache.
  - The planner rejects 18 DUT processes per design (3 per instance) only because they use
    `Call`/`Return` and callable frames, which SystemVerilog members do not support yet.
  - `simulate --duration 1ns` takes about 23 s with or without the kernel, which is
    longer than the full run. Not investigated.

### 2026-10-06: SystemVerilog NBA queue, end-to-end harness

- **NBA queue.** SystemVerilog nonblocking writes use the same ordered queue as VHDL
  assignments. Word slot writes are appended inline by generated code; memory and generic
  writes go in by index, so commit order is exact. This removed about 12M effect helper
  calls; the Verilog run phase went from about 8 s to 7.3 s.
- **Hot-template threshold.** A sweep on Verilog (`FSIM_STATIC_KERNEL_HOT_THRESHOLD`):

  | Threshold | Hot templates | Codegen | Run |
  |---:|---:|---:|---:|
  | 32K | 1 | 1.3 s | 7.3–8.2 s |
  | 8K | 21 | 3.3 s | 6.7 s |
  | 2K | 73 | 8.6 s | 6.0 s |

  The optimizing backend costs more than it saves on fresh caches, so the threshold stays
  at 32K.
- **Harness.** `r37-analysis/tools/e2e.py CASE DIR [--env K=V]` times compile, elaborate
  and simulate in a fresh workspace on CPU 9 and checks sorted Vivado parity.
- **Verilog elaboration profile (8.2 s).** In-place lowering of processes about 18%;
  `collect_occurrences` 10% (it copies a `SpecializedHirUnit` per genvar value per
  instance); constant evaluation about 6%; `HierarchyBuilder::finish` about 15%;
  runtime-state serialization about 13%; publish about 9%.

### 2026-10-06: kernel cost reductions (Phase 2)

- **Host mirrors.** Narrow host signals that members read are mirrored into arena slots and
  refreshed by the input scan, so compiled reads are plain loads. A host write by a member
  requests a re-run only for true sensitivity inputs.
- **Revision-based input scan.** Inputs are compared only when the host's
  `signal_value_revisions` entry changed. `FSIM_KERNEL_CHECK_INPUTS=1` checks that no
  change happens without a revision bump; it found none on the tests or the mixed window.
- **U-aware conditional select and in-place memory reads.** Memories read in place when
  only deferred assignments write them (VHDL).
- **Hot templates.** Templates whose instances × instructions is at least 32K use a second
  LLJIT with the optimizing backend; the light IR pipeline stays.
  - For mixed this is one template, `gf_mult`, which was about 26% of run time.
  - The optimizing backend gives the gain; the IR optimizer alone does not.
  - Kernel time in the 19 µs mixed window went from 2.7 s to 1.9 s; codegen stayed at
    about 0.7 s.
- **Diagnostics.**
  - `FSIM_STATIC_KERNEL_PERF_MAP=1` writes a perf map for the templates and lists template
    → example member.
  - `FSIM_KERNEL_COMPILE_ONLY` / `_SKIP` restrict compilation by member name.
  - `FSIM_KERNEL_NO_MIRRORS` disables host mirrors.

### 2026-10-06: VHDL in the kernel (Phase 2)

1. **Planner.**
   - VHDL processes become members; the general single-wait shape has its first run from
     op 0 and resumes after the wait.
   - New op support:
     - zero-delay inertial `WriteProjected*` and generic-domain `WriteUpdate*`;
     - shared-variable blocking writes;
     - calls with fixed or dynamic stacks;
     - isolated automatic frames;
     - integer ops and checks;
     - assertions.
   - Ownership: Logic9 signals and resolved `std_logic` with exactly one driver per element.
     Writer ranges come from region-graph accesses.
   - A kernel runs in one language mode. In VHDL mode it is admitted only when every VHDL
     process is a member and no member reads an unowned signal that another member writes.
2. **Exact delta mode.**
   - Every assignment is deferred to the end of the round, so one round equals one VHDL
     delta.
   - Signals that changed relative to the start of the round notify once.
   - Outputs are published in the generic domain.
3. **Witness test.** `tests/app/static_kernel_vhdl_test.cpp` covers:
   - package functions with loops;
   - expression port actuals;
   - a delayed clock that samples through an adapter and through a plain signal
     (shoot-through);
   - a derived clock;
   - a shared-variable RAM;
   - weak `H`/`L` values;
   - disjoint resolved drivers;
   - X injection.

   It runs on both engines. Making VHDL writes immediate (a test mutation) makes it fail.
4. **Compiled tier for VHDL (KIR + LLVM).**
   - New instructions:
     - `load_slot9`, `load_field9`, `store_vhdl`;
     - integer ops and checks;
     - `call`/`ret` with return dispatch;
     - `assert_check`, `deopt`.
   - **Deoptimization.** A value the compiled code cannot represent hands the rest of that
     run to the reference evaluator. Registers are synchronized in both directions; only
     registers live at the entry must round-trip.
   - **'U' tracking.** Shadow masks propagate through moves, inserts, VHDL `and`/`or`/`xor`/
     `not`, loads and stores. Comparisons, arithmetic and branches treat 'U' as X, as the
     reference does.
   - **Width analysis.** SimIR reachability from the compiled entry and liveness remove
     dead constant loads (function-result placeholders, prologue initializers). This
     removed register-width polymorphism.
   - **Inline paths:** deferred stores, dynamic bit and part inserts, and 32-bit integer
     add, subtract and multiply.
5. **Bugs found by verification and fixed.**
   - A branch on a polymorphic register ran through the generic bridge, which drops the
     branch target.
   - A dynamic-call width rule gave register 0 a width of 32.
   - Native stores lost the 'U' mask.
   - A copy into a Logic4 register must coerce 'U' to X.
6. **Measurements (mixed, full run).**
   - Simulate phase, against the reference engine's 21.5 s:

     | Step | Simulate |
     |---|---:|
     | Generic tier only | > 170 s for 1.9k cycles |
     | Compiled | 33.7 s |
     | + 'U' tracking | 28.4 s |
     | + inline stores, no polymorphism, Logic9 field reads | 17.0 s |

   - Kernel: 17.3M member runs in 12.7 s, about 730 ns per run.

### Before 2026-10-06 (Phase 1 summary)

- Verilog static kernel with compiled and native tiers, partitions and canonical templates.
- The run phase went from 16.7 s to 8.1 s. Setup is about 2.0 s, of which codegen is about
  1.0 s.
- Portable source-path identity cache: compile time went from 7.0 s to 0.9 s.

## 5. Open issues and decisions

1. **Same-time-step print order.** Testbench instances 4 and 5 finish in the same time step,
   so their `THRU` lines can come out in either order. The reference fsim engine (without
   the kernel) already prints them in the opposite order from Vivado, on both designs. IEEE
   1800 leaves process order within a region undefined, which makes this a race under D4.
   Parity is compared as a set.
2. **Report order within a delta.** In 50 of about 9.6k reports, metavalue warnings come out
   in a different order inside a single delta. VHDL leaves process order within a delta
   undefined. Counts per location match exactly.
3. **Time-0 order of VHDL initialization against SystemVerilog `initial` blocks.**
   `rs-vhdl` testbenches assign their stimulus registers in an `initial` block at time 0.
   - The reference engine happens to run some VHDL initialization before them and some
     after.
   - The kernel runs all VHDL initialization in one round.
   - The functional transcript is identical. Only time-0 metavalue warning counts at one
     location differ (one per decoder instance).
   - Neither LRM orders the two, so this is a race under D4.
4. **`rs-vhdl` testbenches that do not elaborate** (front end, on HEAD too):
   `rs_decoder_tb` (`fork` around task calls with array arguments), `rs_encoder_tb` (VHDL
   `maximum` in a generate-scoped constant), `rs_symsize_tb` (slice assignment in a
   for-generate).
5. **Time-limited runs are slow.** `simulate --duration` takes about 24 s on Verilog even
   for 1 ps (the full run to `$finish` takes 10 s). The cause is in the time-limit exit
   path. It does not affect the benchmarks.

## 6. Next steps (in order)

1. **Conditional yield at mixed boundaries.** Continue internally when no kernel output
   changed and the host has nothing else pending in the time slot. This needs a correct
   "time slot otherwise idle" query from the scheduler and would recover about 0.3 s on
   mixed.
2. **Verilog simulation toward Phase 1 (≤3 s).** By wall time, startup is about 2 s,
   template code generation about 2 s, testbench start about 1 s, and the run about 5 s.
   - Startup: runtime-state decode, `from_state` region graph and
     `compute_signal_driver_inventory`, `create_interpreter` validation and driver
     registration, kernel construction and `plan_static_kernel`.
   - Code generation: about 2 s for 384 templates at O0. Smaller IR per KIR operation,
     or interpreting cold templates instead of compiling them.
   - Run: `slot_changed` and `native_notify` (about 10% of run instructions), host
     scheduler and commit traffic from testbench processes.
   - Phase 1 also calls for a v4 behavioral tier for testbench processes (plan §3.5),
     the long-term home for the host-side costs.
3. Elaboration: runtime-state serialization (about 0.5 s), constant evaluation (about
   0.3 s), process canonicalization (about 0.37 s), lowering (about 0.85 s).
4. **VHDL delta-depth collapsing** (plan §3.2), now constrained by the per-round host
   yield: collapse only rounds whose outputs no host process reads.
