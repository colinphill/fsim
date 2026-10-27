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

## 2. Current status (2026-10-07)

**2026-10-25 (Stage D, retirement bundles 1-2):** the legacy per-process LLVM JIT
is deleted from the non-test code (`src/compiler/llvm_jit*`, `src/compiler/llvm/`,
the `application_executor*` services, the app-layer JIT setup block and AOT
receipts). `--engine compiled` is an alias of v4; `fsim_compiler` holds only
`static_kernel_codegen.cpp`. The region/frontier runtime (bundle 3) is still
present but unreachable. The numbers below predate the removal.

Paired qualification `v4-p1-pair-10071004` (7 pairs, CPU 9, fresh caches, frozen
candidate `.local-artifacts/v4cand-p1-10071004`) against the HEAD control:

| Case | Leg | Compile | Elaborate | Setup + simulation | Total |
|---|---|---:|---:|---:|---:|
| Verilog | v4 | 0.59 | 2.85 | **2.95** | **6.38** |
| Verilog | HEAD | 0.64 | 5.60 | 24.82 | 31.21 |
| Mixed | v4 | 1.13 | 2.85 | **3.30** | **7.27** |
| Mixed | HEAD | 1.23 | 3.20 | 20.85 | 25.33 |

Phase 1 exits with these numbers. D6 (about 5.6 s and 4.6 s totals) is still open.
**D6 throughput criterion met.** Two paired campaigns after the D6 work (§4) each time
xsim in the same session:

| Campaign | Mixed fsim / xsim | Speedup | Verilog fsim / xsim | Speedup |
|---|---:|---:|---:|---:|
| `v4-d6a-pair-1007` | 4.73 s / 15.62 s | 3.31× | 4.59 s / 19.54 s | 4.26× |
| `v4-d6b-pair-1007` | 4.82 s / 15.10 s | 3.13× | 4.94 s / 18.63 s | 3.77× |

The xsim figures are the preflight's single samples; the fsim figures are medians of 7
pairs.
- **Correction (corpus run).** With 3 xsim samples per case (`v4-corpus-1007`, below),
  mixed_throughput's xsim median is 12.82 s. Against it, the D6 candidate reaches only
  2.85× (4.50 s), not 3.13×. The single 15.10 s preflight sample overstated the margin.

**D6 on the corpus (`v4-corpus-1007`, frozen candidate `v4cand-d6b-1007`).** These are
`perf_campaign.py` runs, 3 samples each, CPU 9, `FSIM_STATIC_KERNEL=1`. All ten cases
pass stimulus parity against Vivado.

| Case | fsim | xsim | Speedup |
|---|---:|---:|---:|
| original_throughput | 4.43 s | 15.80 s | 3.57× |
| mixed_throughput | 4.50 s | 12.82 s | 2.85× |
| codex (6 cases) | 2.93–3.13 s | 8.74–9.89 s | 2.92–3.31× |
| mixed_codec | 19.54 s | 25.05 s | 1.28× |
| original_codec | 30.90 s | 27.64 s | 0.89× |

D6 is therefore **not met on the corpus**: the codec cases miss widely, and
mixed_throughput and three codex cases sit just under 3×. Work in progress is logged
in §4 (2026-10-07: corpus).
- **Same-session comparison.** The second campaign ran on a slower VM: the HEAD control
  took 5–7% longer. Comparing against xsim timed in the same session removes that drift.
- **Earlier targets.** The 5.6 s and 4.6 s targets assumed 16.9 s and 13.8 s for xsim;
  the campaigns measure 15.1–19.5 s.
- **Open: D6's second clause** (simulation within 2× of a 2-state cycle-based
  reference) is **measured and not met** (§4, 2026-10-07 Verilator reference).
  - Verilator 5.052 runs the Verilog throughput fixture in 0.30 s.
  - fsim's simulate phase takes 2.65 s, 8.8× Verilator; its run phase alone takes
    1.67 s, 5.5×.
  - The owner deferred the clause to Phase 6 on 2026-10-07. Until then, D6 is the
    first clause on the corpus.
  - The wider corpus is not assembled yet. The older table below records the
    2026-10-06 state.

### 2026-10-06 (late) state

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
| 1 Verilog slice | exact parity, simulation ≤3 s, total ≤10 s | **met** on 2026-10-07 (paired run `v4-p1-pair-10071004`): Verilog setup and simulation 2.95 s, total 6.38 s; mixed total 7.27 s; Verilog/mixed 0.88; both transcripts identical to the reference engine, Vivado preflight passed |
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

### 2026-10-27: end-of-plan perf, elaboration cuts (`d4ab0a42`, `ea052048`, sweeps `v4c16`, `v4c17`)

The design-level phases are done, so the perf work held for the end of the plan begins. It targets elaboration, the largest phase. The baseline is `v4c15`; a callgrind profile of `original_codec` elaborate without `--aot` showed 51.6G instructions.

**Kernel codegen at elaborate (`--aot`).**
- **Lazy operation tiers.** A compiled member, VHDL included, no longer gets an AOT operation tier. A VHDL member that deoptimizes compiles its tier on demand; the perf cases have no deopts. On mixed_codec this removed a 480-tier batch (0.62 s).
- **VHDL shape sharing.** VHDL units now join step-2 shape groups. Where group members differ in a `load_field`, `load_field9` or `store_vhdl` offset, the offset becomes a binding: `with_bound_offset` at run time, a bound-offset plane read and a deferred store in codegen. Constants stay unparameterized when a VHDL unit carries a `'U'`-tracking flag (`imm_b`), so the two-state rule for unknown constants holds.
- On mixed_codec: eager templates 1963 → 864, eager KInsts 250k → 173k, full templates 1256 → 636, native stage 4.77 → 3.31 s.

**Trusted reload.** `elaborate --aot` reloads the design it has just published through `load_design_artifact_for_simulation`, which is trusted, as in simulate. Artifact load: original_codec 1.27 → 0.53 s, mixed_codec 0.37 → 0.16 s.

**Frontend.**
- `truth()` in the constant evaluator reads the aval/bval planes a word at a time instead of calling `get_logic9` per bit. That was 13.4M calls on original_codec.
- `pack_container_signal_value` writes four-state leaves into the planes a word at a time instead of one `insert_bits` per leaf. That was 2.6M calls (1.76G) from declaration materialization.
- **Library-object checksums** use XXH64 (owner, 2026-10-27). The checksums of compiled-HIR, native and auxiliary payloads are now `xxh64:`; verifying them with SHA-256 cost about 1.5G per codec elaborate. Validators accept either spelling.
  - Source entries stay SHA-256, because an object source's checksum is also its `CheckedSource` content digest. That digest must equal the direct path's, or specialization cache keys depend on how the design was loaded (`mixed_conversions` test).

**Measured** (perf stat, elaborate with `--aot`, quiet host):

| Case | v4c15 instructions | v4c16 instructions | v4c15 task-clock | v4c16 task-clock |
|---|---:|---:|---:|---:|
| mixed_codec | 113.5G | 87.8G | 11.55 s | 9.1–9.6 s |
| original_codec | — | 89.0G | 12.20 s | 11.3 s |

Simulate is unchanged: instruction counts are within 1%, and simulate compiles no new code. The three perf cases are byte-identical; object paths in messages differ only by digest.

**What remains in elaboration** (callgrind, without `--aot`):
- constant-function evaluation of generate-local localparams: about 11.5G, from loop-heavy GF(2^m) functions (`alpha_pow`, `calc_alpha_pow`) evaluated per generate iteration;
- HIR process lowering: about 16G;
- runtime-state encoding: about 4.3G;
- `CacheKeyBuilder` SHA-256: about 0.37G.

**Long constant-bound `for` loops** (`ea052048`). Sweep `v4c16` timed out on verilator `t_for_local`. That case had been slow since `v4c3`: 4.6 s before, 37–57 s after, and a timeout at `v4c11` as well.
- Lowering unrolled a SystemVerilog `for` loop with constant bounds whatever its length, so the case's 100000-iteration loop became an 800k-operation body.
- Elaboration took 7 s. Since `v4c3` compiles every member natively, simulate also spent 31 s in LLVM on that body.
- Past 256 iterations, the cap `repeat` loops already use, such a loop now takes the existing run-time `for` lowering used for dynamic bounds (IEEE 1800-2023 12.7.1). `foreach` keeps its unrolled form, because run-time `foreach` covers strings only.
- The case now takes 0.05 s to elaborate and 0.06 s to simulate.

Full ctest passes 1085/1085 in normal and strict mode for both commits.
- **Sweep `v4c16`:** no losses against `b96x1` except the `t_for_local` timeout.
- **Sweep `v4c17`:** no losses against `b96x1`.
  - Passes: nvc 1259, vhdl-compliance 65, vests 3663, ivtest 2640, verilator 2071, sv-tests 1495.
  - The two nvc cases `case11` and `issue705` remain excluded (strict 10.9 reading, owner).

### 2026-10-27: design-level phase 5, per-signal access plan (trace selection) (`9adf4431`, sweep `v4c15`)

Until now, a run that wrote a trace file set `observe_all`, which made every signal a host signal. Plan §3.6 asks for a per-signal plan: only what a client can observe needs the host's storage.

**Selection.** The CLI `simulate` knows its trace selection before planning, and the selection is fixed for the run. `plan_trace_access` (app) computes it from the same rules `attach_trace` applies:
- signal-bearing DesignIR objects;
- the `--trace-filter` globs, matched against both the design path and the canonical FST path, so the result is a superset of either format's selection.

The result is `StaticKernelAccessPlan::observed`, which replaces `observe_all`. Its SHA-256 (`observed_identity`) joins the persisted-plan key.

**What stays global:**
- The debugger, Tcl sessions, the library API and foreign libraries keep `foreign`/`observe_all`, because their access is not known in advance.
- `$dumpvars` (a run-time observer) keeps `observe_all`.
- With an observed set, dead-logic removal, VHDL delta collapse and time warp stay off.
- An opaque VHDL process widens the set to every signal, as before.
- `FSIM_STATIC_KERNEL_TRACE_SELECTION=0` restores whole-design observation.

**Planner.** The observed signals are marked `host_touched`, exactly as signals a host process writes. Everything downstream already reads that per signal.

**Exactness fixes found on the way.** Both concern a member's NBA to a host signal. The reference applies every NBA of a time step, then runs what they wake, in the same delta.

1. **Split commit.** With only some signals on the host, one clock edge can give a member NBAs to both a host signal and a kernel-owned signal. Before this fix:
   - the host applied the host-signal NBA in its own NBA region;
   - the kernel committed its own NBAs and settled in its activation, ahead of the host's;
   - so a net reading both (`arr[1] = acc ^ rd` in the test) changed twice, a delta apart.
2. **Late wake.** With every signal on the host, kernel readers of such a signal ran again a delta late, through `yield_requested_` and the next delta. The test fixture saw this at its mid-run reset.

Now a member's NBA-region update to a host signal (`host_nba_staged_`, `host_nba_wakes_`) defers the kernel's NBA region into the host's update phase:
- the deferred task is ordered last (`StableOrder` max);
- the task first scans the host's applied updates, then commits the kernel's own writes, then settles.

The SystemVerilog regions of a mixed (VHDL) kernel (`sv_drain`) keep the previous behavior.

**Test.** `static_kernel_test` traces five signals of its fixture: a wide register, a race register, the net above and two more. It requires:
- the selection plan owns 20+ more signals than the all-host plan;
- under both the all-host plan and the selection plan, every traced signal changes exactly as in the reference engine (value, time and delta).

**Glitches remain a race.** Compared as VCDs against the reference, both plans also match every signal's value at each time step on the SV fixture and on a mixed variant with one VHDL instance. What still differs is intermediate values within a time step on combinational chains (`dead0`, `dead1`, `dead_parity`), plus the time-0 record order:
- the reference evaluates continuous assignments event by event, in its process order;
- the kernel settles levelized;
- IEEE 1800-2023 4.7 leaves that order arbitrary, so these are races under D4, like open issue 3.

v4c14 gives the same differences.

Full ctest passes 1085/1085 in normal and strict mode. The three perf cases are byte-identical. Sweep `v4c15` has no losses against `b96x1`. Passes: nvc 1259, vhdl-compliance 65, vests 3663, ivtest 2640, verilator 2071 (one more than `v4c14`), sv-tests 1495.

### 2026-10-27: design-level phase 4, sequential dispatch groups (`26b04641`, sweep `v4c14`)

Until now, an edge scheduled every sequential member sensitive to it, one by one. Each `schedule()` set the member's flag and pushed it to the triggered list. The Active pass then sorted that list and ran the members in index order. The owner's condition (2026-10-27) is that grouping must keep that order.

**Groups.** `build_dispatch_groups` forms a group from sequential SystemVerilog members that meet both conditions:
- they share an identical edge-sensitivity set, such as `posedge clk` or `posedge clk or negedge rst_n`;
- they have no other wake-up: no any-edge or ranged readers, and they are not VHDL.

Each slot or input edge list then holds one reader per group and edge in place of the members' readers. An edge does one `edge_matches` check and queues one group token.

**Running.** `run_triggered` drains the triggered list in one of two ways:
- **Group tokens only (up to 8).** It merges the groups' member lists by member index, which is the same order the sorted list gives, with no flags and no sort.
- **Otherwise.** It expands each group into its members, skipping members already pending (their flags dedupe), then sorts and runs as before.

**Exactness.** Two cases fall back to per-member `schedule()`, so the member flags give today's semantics:
- *An edge on a group that is being merged.* The members that ran (or are running) in this pass are scheduled again; the rest are still pending.
- *An edge on an expanded group, or one arriving while a mixed round commits.* The members are scheduled individually; in the mixed case they wait for the next delta as before.

`FSIM_STATIC_KERNEL_DISPATCH_GROUPS=0` disables grouping, and `FSIM_PROFILE_KERNEL` prints `dispatch_groups=`. `static_kernel_test` adds two registers on `posedge clk or negedge rst_n` whose blocking writes read each other. Their values depend on the members' run order and must match the reference mid-run and at the end.

**Measured** (simulate instructions:u, against `v4c13`); outputs are byte-identical on all three cases:

| Case | v4c13 | Groups | Change |
|---|---:|---:|---:|
| original_codec | 29.24G | 28.27G | −3.3% |
| mixed_codec | 28.50G | 28.52G | flat |
| mixed_throughput | 25.74G | 25.72G | flat |

On original_codec, all 253 sequential members form one group, and schedules fall from 24.8M to 17.2M. The mixed cases' clocked logic is VHDL, so they have no groups.

Full ctest passes 1085/1085 in normal and strict mode. Sweep `v4c14` has no losses against `b96x1`. Passes: nvc 1259, vhdl-compliance 65, vests 3663, ivtest 2640, verilator 2070, sv-tests 1495.

### 2026-10-27: design-level phase 3, native wide values (`ed621e15`, sweep `v4c13`)

Before this change, every register operand or result wider than 64 bits went through the generic fallback. That fallback copies operands into the reference register file as `PackedLogic4` and runs the reference step. The only exceptions were field slots, family proxies and the source of a narrow selection.

**Storage.** A *chunked* register has a fixed width above 64 and is not polymorphic, not a field slot and not a family proxy. It keeps its SimIR index for analyses. Its storage moves to the end of the instance register file:
- `CompiledBody::wide_base[r]` holds the first storage index, plus a flag when masks are present;
- chunk `j` is `registers[base + j]` as `{aval, bval}`;
- 'U' mask words sit at `registers[base + n + j]`.

Every chunked VHDL register has masks. A VHDL register can hold Logic9 values whatever its declared kind, because only ReadSignal, LoadConstant and CopyRegister coerce to the kind. SV Logic9 registers have masks too. Bridges assemble and split `PackedLogic4` for generic operands, deopt, verify and the behavioral tier.

**Ops.** Each SimIR op still lowers to one KInst. The new ops replace the generic path for ReadSignal, CopyRegister, LoadConstant, Concatenate, DynamicPartInsert and signal writes:
- `wide_load` reads a signal; a Logic9 slot is read exactly, or coerced for a Logic4 register;
- `wide_copy` copies a register;
- `wide_const` loads from the `body.wide_constants` pool;
- `wide_concat` builds a value from narrow or wide operands;
- `wide_store` writes a signal per chunk, as an SV blocking or NBA write, or a VHDL `store_vhdl` write with 'U';
- `wide_part_insert` inserts a narrow value at a dynamic position.

Extract, dynamic_extract and dynamic_part_select read chunked sources in place (`sub 3`). Codegen emits copies, constants, exact loads and static extracts inline. It calls `fsim_sk_wide_<op>` helpers for the rest. Arithmetic, compares, shifts, conditionals, dynamic slice writes, proxies and host signals stay generic.

**Partitions and caches.** Partition concatenation rebases `wide_base` and appends `wide_constants`. The native object cache key (`template_key`) now includes `wide_base`, `wide_constants` and concat 'U' operands. Without them, partition templates that differed only in their wide constants shared code; this broke original_codec. `verify_run` merges adjacent chunk writes to a slot so they compare against one reference write. `FSIM_KERNEL_WIDE_MASK` (diagnostic) selects the wide lowerings: bit 0 ReadSignal, 1 CopyRegister, 2 LoadConstant, 3 Concatenate, 4 DynamicPartInsert, 5 writes.

**Tests.** Both static kernel tests gain wide registers:
- SV: an NBA register, a blocking concat and a dynamic part-select;
- VHDL: a wide variable process with a 132-bit concat, and an entity instantiated twice whose wide constant depends on a generic, so the two instances share a template but not constants.

**Measured** (simulate instructions:u against `v4c12`); outputs are byte-identical on all three cases:

| Case | v4c12 | v4c13 | Change |
|---|---:|---:|---:|
| original_codec | 29.25G | 29.24G | flat |
| mixed_codec | 29.82G | 28.50G | −4.4% |
| mixed_throughput | 26.98G | 25.74G | −4.6% |

Generic runs on mixed_codec fall from 382,332 to 22,641.

Full ctest passes 1085/1085 in normal and strict mode. Sweep `v4c13` has no losses against `b96x1`. Passes: nvc 1259, vhdl-compliance 65, vests 3663, ivtest 2640, verilator 2070, sv-tests 1495.

### 2026-10-27: design-level phase 2, VHDL delta collapse (exact-only subset) (`11dc5c30`, sweep `v4c12`)

The owner chose the exact-only subset on 2026-10-27. A collapsed VHDL combinational member runs in the update phase that woke it, instead of in the next round. Other members still use one round per delta.

**Planner (elaboration, plan v6).** A member is admitted only if all of these hold:
- it is a VHDL combinational member that runs at start, is not lazy, uses no container and has known dependencies;
- its any-edge sensitivity covers every signal it reads (owned or constant inputs, no aliases);
- it is the sole writer of plain owned outputs that nothing observes;
- every reader of those outputs is a collapsed member or a **first-delta** member, i.e. one whose sensitivity holds only time-only signals.

A **time-only** signal is written only by delay-only SystemVerilog threads (static `#d > 0` waits, no event controls, a whitelist of operations) or by time-zero initializers, or it is a constant input. Such a signal changes only before a time step's first round, so a first-delta reader never sees the deltas in between. Admission repeats to a fixed point. Kahn's algorithm then drops cycles.

**Kernel.** It drops a planned member when any of these apply:
- a host boundary or hosted process could run between rounds;
- the compiled code is impure: loops, state carried between runs, checks or assertions that could fail, generic or integer operations, calls, or partial or dynamic stores.

A drop propagates to the collapsed writers of what the member reads. Collapse is off at time 0 and when `FSIM_STATIC_KERNEL_DELTA_COLLAPSE=0`. Diagnostics: `FSIM_PROFILE_KERNEL_PLAN` prints admission and reject reasons, `FSIM_PROFILE_PHASES` prints kernel drop reasons, and `FSIM_PROFILE_KERNEL` prints `collapsed=`.

**Region graph.** `RegionSignalNode::writes_undeclared` separates undeclared or unattributable writes from declared writes whose range is unproven (`ownership_mismatch`). Those unproven ranges no longer mark the signal unknown-observed. Existing consumers still use `writers_unknown`.

**Measured on mixed_codec:**
- the planner admits 553 members; the kernel keeps 335 in 3 levels;
- kernel drops: 92 code shape (calls), 36 assertions, 23 generic, 22 `integer_binary`, 11 `integer_check`;
- 175,157 runs move into the update phase that woke them;
- outputs are byte-identical to `v4c11` on both mixed cases;
- simulate instructions are unchanged: mixed_codec 29.72G against 29.68G, mixed_throughput 26.88G against 26.85G.

Moving runs earlier saves no round, because each delta still holds non-collapsed members. The planner's remaining rejections are:
- 2,564 outputs with several disjoint writers (slice drivers);
- 1,146 port-conversion driver readers;
- 2,356 cascaded from rejected readers.

None of these can be admitted without relaxing the exact rule. `static_kernel_vhdl_test` adds a two-member chain read only by a clk register; it must collapse and match the reference exactly.

Full ctest passes 1085/1085 in normal and strict mode. Sweep `v4c12` has no losses against `b96x1`. Passes: nvc 1259 (the two owner-excluded cases), vhdl-compliance 65, vests 3663, ivtest 2640, verilator 2070, sv-tests 1495.

### 2026-10-27: design-level phases measured (constant propagation rejected)

This entry covers the remaining design-level items measured against the perf cases, using instructions:u and kernel counters on `v4c11`.

**Constant propagation across bindings: tested and rejected.** The patch is saved as `.local-artifacts/v4-migration/design-opt/constant-propagation-rejected-2026-10-27.patch`. It had four parts:
- one shared evaluator for constant programs;
- `initialize_constant_driven_nets` extended to merged constant drivers and to tied-port actuals. Continuous assignments and `always_comb` evaluate at time zero, so they no longer count as waiters needing the time-0 event;
- a planner list of signals that stay at their constant;
- members' `ReadSignal` of those signals rewritten to `LoadConstant`.

On original_codec it folded 8,544 signals into 63,257 members, and outputs stayed byte-identical. It still does not pay:

| Variant | original_codec sim | original_throughput sim | Elaboration |
|---|---:|---:|---:|
| Constants bound per instance (template sharing kept) | +0.4% | +1.3% | +1.1%, +3.1% |
| Constants as immediates (LLVM folds them) | +1.3% | +2.2% | +38%, +26% |

In the bound variant, partition templates went from 550 to 795 because tied and untied instances now differ in shape. The ALU work in the folded operands is not where the run time is.

**Port and alias collapsing: already done.** Elaboration aliases a port connected to a simple net, so a hierarchy plans the same members as its flat equivalent. Explicit whole-signal copies (`assign y = x`) are 483 of 96,886 combinational members in original_codec, 209 of 50,746 in original_throughput, and 0 in mixed_codec.

**Sequential supernodes.** Every perf case has one trigger set: 253 sequential members in original_codec and mixed_codec, 152 in original_throughput, 45 in codex_throughput_default. Grouping them saves only per-member dispatch, about 7.65M schedule and run calls, or 2–3% of original_codec simulate. It would also reorder same-level Active-region evaluation, which race-order corpus cases see.

**Wide values.** The remaining wide work on the kernel's generic path is under 1% of simulate. On mixed_codec it is about 350k reference operations against 17M member runs: 256- and 264-bit dynamic part inserts, reads and copies. original_codec has no wide members.

**VHDL delta collapsing.** On mixed_codec the per-delta machinery is the largest cost:
- `commit_round` takes 15% of simulate instructions;
- `activate_mixed`, `sort_round`, `schedule` and `store_planes_word` take another 7%;
- VHDL combinational runs are 9.1M, against 0.55M combinational plus 1.67M partition runs for the Verilog build of the same design.

An exact as-if collapse needs every observer of a collapsed signal to be blind to its delta. A process woken in the same delta by another event, such as a flop on a delta-delayed clock (`clk_int <= clk`) when a testbench input changed in that delta, reads the collapsed value one or more deltas early. A process sensitive to a collapsed chain's output runs once instead of once per glitch. Neither can be ruled out statically in general. This needs an owner decision on its semantics.

### 2026-10-27: design-level phase 1, dead logic as lazy members (`6adcb28e`, sweep `v4c11`)

The planner runs a liveness pass over the region graph.
- **Roots:** every process that cannot be lazy, every signal the host owns, touches or observes, and aliased containers.
- **Propagation:** liveness flows from a live process to what it reads, and from a live signal or container to its writers.
- **Cycles:** a member on an activation cycle stays live (Tarjan over the dead candidates' trigger edges).

A dead member becomes **lazy** only if it meets all of these:
- it is a SystemVerilog combinational member that runs at start;
- it has only immediate writes (blocking, or a continuous assignment's Active update);
- it uses no container;
- its any-edge sensitivity covers every bit it reads.

A lazy member is never scheduled. `StaticKernel::materialize` evaluates it from current inputs when one of its outputs is read, after its lazy inputs' writers. This keeps §3.6 on-demand reads exact.

Removal is off when any of these apply:
- foreign, trace, coverage, debug and publish-all runs;
- the access inventory is incomplete;
- `FSIM_STATIC_KERNEL_DEAD_LOGIC=0`.

The persisted plan format is v5.

Lazy members: original_codec 83 of 99,752, original_throughput 23, codex_throughput_default 11. The mixed cases have none, because VHDL members are excluded. Results on the 10 perf cases:
- outputs are byte-identical to `v4c10`;
- simulate instructions are within +0.03%, so these designs carry little dead logic;
- time warps are intact.

`static_kernel_test` adds an unread chain. It must be lazy, and it must read back exactly mid-run and at the end. Full ctest passes 1085/1085, normal and strict.

Sweep `v4c11` against `b96x1`:
- **Losses:** none in five suites. nvc `case11`/`issue705` are excluded as before. verilator `t_for_local` timed out under the load of a concurrent profile; alone it passes on both `v4c11` (31.2 s) and `v4c10` (31.6 s).
- **Gains:** the same 5 ivtest and 5 verilator gains as `v4c10`.

Profile of original_codec simulate on `v4c11` (instructions:u):
- native code 40%;
- notification and scheduling about 17% (`native_notify`, `slot_changed_general`, `write_slot_word`, `schedule`);
- setup and snapshot decode about 20%.

Kernel counters for original_codec:

| Counter | Value |
|---|---:|
| sequential runs | 7.65M |
| combinational runs | 0.55M |
| partition runs | 1.67M |
| schedules | 24.8M |
| notifications | 15.1M |
| once members | 2,311 |

Simple port connections are already aliased at elaboration: a hierarchy and its flat equivalent plan the same members. Port collapsing is therefore mostly done. The next phase is constant propagation of tied ports such as `gf_mult`'s constant `b`.

### 2026-10-26 (night): G5 campaign `v4-g5-1026b` on `v4c10` (db81054a), rs-suite check

The timed campaign used 3 samples, CPU 9, ASLR off, with xsim in the same session and nothing else running. Preflight passed. Against the D6 exit criteria:
- every simulate ratio is now at least 3× xsim;
- the geomean end-to-end ratio is 3.19× (3.17× in `v4-g5-1026a`).

The tool's own `passed_timing` flag covers only the `llvm_o2` configuration, so it stays false for `v4_o2_aot`.

| Case | e2e × | fsim sim s | xsim sim s | sim × (1026a) |
|---|---:|---:|---:|---:|
| original_codec | 2.22 | 2.385 | 7.652 | 3.21 (3.17) |
| original_throughput | 3.01 | 1.681 | 7.753 | 4.61 (4.60) |
| mixed_codec | 2.05 | 2.134 | 6.701 | 3.14 (2.98) |
| mixed_throughput | 2.58 | 1.683 | 6.147 | 3.65 (3.49) |
| codex_reference_mode0_frames1 | 3.84 | 0.127 | 3.940 | 31.03 |
| codex_reference_mode0_frames2 | 3.85 | 0.128 | 4.041 | 31.46 |
| codex_reference_mode1_frames1 | 3.94 | 0.128 | 3.991 | 31.12 |
| codex_reference_mode1_frames2 | 4.01 | 0.128 | 3.941 | 30.88 |
| codex_throughput_default | 3.64 | 0.429 | 4.041 | 9.42 |
| codex_throughput_direct_syndrome | 3.63 | 0.377 | 4.038 | 10.70 |

**G5 (b).** The rs suite was elaborated with `b96x` and with `v4c10`, interleaved, 2 repetitions on CPU 9; the comparison uses the minimum over the repetitions:
- 8 testbenches × VHDL/Verilog: instructions −5.4% to +1.3%, CPU −5.9% to +3.2% (the largest CPU increases are on sub-second cases);
- `rs_param_tb` VHDL: 13.55 → 13.79 s (+0.3% instructions);
- `rs_param_tb` Verilog: 18.72 → 18.02 s;
- the four frontend compile jobs: unchanged or faster.

Both binaries return rc=1 for `rs_decoder_tb` VHDL and `rs_param_tb` Verilog. In `rs_decoder_tb`, the Verilog testbench leaves VHDL input `parity_syms` unassociated, and that port has no default (FSIM-ELAB-BIND-027). These are workload issues, not regressions.

**G2/G3.** The six-suite `--engine v4` sweep `v4c10` was compared against `b96x1`. It has 0 losses, apart from nvc `case11` and `issue705`, which are excluded under the strict 10.9 reading (owner). It gained 5 ivtest cases (br1008, br988, fork3.19A, fork3.19B, pr1862744a) and 5 verilator cases (t_interface_input_port_assign, t_interface_input_port_assign_noinl, t_math_eq, t_tri_select_eqcase, t_udp_sequential).

The census (`census/v4c10`) found:
- host processes 0 in every suite;
- all 7,648 kernel runs report `interpreted=0`;
- 36 kernel deopts across 9 cases, all on the operation tier.

| Suite | b96x1 | v4c10 | Planned = admitted = host=0 |
|---|---:|---:|---:|
| ivtest | 2635 | 2640 | 2273 |
| sv-tests | 1495 | 1495 | 387 |
| verilator | 2065 | 2070 | 2087 |
| vests | 3646 | 3646 | 1606 |
| nvc | 1261 | 1259 | 1208 |
| vhdl-compliance | 65 | 65 | 65 |

G1-G5 are all met on `v4c10` (db81054a). The design-level optimizations follow as v4 phases (owner, 2026-10-26).

### 2026-10-26 (night): mixed_codec deopts removed

`mixed_codec` failed the per-case simulate clause at 2.98× because 7,613 kernel deopts ran members on the operation tier. Almost all of them came from `gf_mult` under the Euclid key-equation solver, where `'U'` operands reach code that only handles Logic4. Two changes clear every deopt in both mixed cases:

- **Register-source selections.** `DynamicExtract` and `DynamicPartSelect` with a Logic4 register source are `'U'`-aware. The selected 'U' bits pass into a tracked destination; the index is an integer and is never `'U'`. Tracking propagation marks such a destination tracked when its source is, so 'U' no longer reads as X.
- **Case equality.** VHDL `=` on `std_ulogic` lowers to `case_equal`. That op now also compares the operands' 'U' masks in codegen and in the bytecode interpreter, so it no longer requests a 'U' check.

`commit_round` also prefetches upcoming commit slots and arena values. `bodies_magic` is now `fsim-static-kernel-bodies-v3`.

Results:

- Deopts fell from 7,613 to 0 (`mixed_codec`) and from 8,205 to 0 (`mixed_throughput`).
- Simulate output is byte-identical against the `v4c9`-equivalent replay on all ten campaign cases.
- On the quiet host, the interleaved simulate median over 7 runs (CPU 9, ASLR off) against `v4c9` was:
  - `mixed_codec`: 2.265 → 2.162 s (−4.5%);
  - `mixed_throughput`: 1.735 → 1.660 s (−4.3%);
  - `original_codec`: 3.185 → 3.115 s (−2.2%).

### 2026-10-26 (evening, host quiet): G5 campaign `v4-g5-1026a` on `v4c9` (e4373678)

e4373678 committed the parked setup and cache cuts. Object cache envelope V2 uses XXH64, and older
envelopes miss. Against `v4c8`, instruction counts (instructions:u) fell by 1.0-5.6% for elaborate
and 0.3-5.2% for simulate, with simulate output byte-identical. Full ctest passed 1085/1085 in normal
and `FSIM_V4_STRICT=1` mode.

The timed campaign (3 samples, CPU 9, ASLR off, xsim in the same session) passed preflight but not
timing:

| Case | fsim e2e s | xsim e2e s | e2e × | fsim sim s | xsim sim s | sim × |
|---|---:|---:|---:|---:|---:|---:|
| original_codec | 12.67 | 27.96 | 2.21 | 2.33 | 7.40 | 3.17 |
| original_throughput | 5.25 | 15.87 | 3.03 | 1.63 | 7.50 | 4.60 |
| mixed_codec | 12.30 | 24.62 | 2.00 | 2.18 | 6.49 | **2.98** |
| mixed_throughput | 4.97 | 12.59 | 2.53 | 1.73 | 6.04 | 3.49 |
| codex_reference_mode0_frames1 | 2.41 | 9.23 | 3.82 | 0.13 | 3.84 | 29.98 |
| codex_reference_mode0_frames2 | 2.42 | 9.28 | 3.84 | 0.13 | 3.84 | 30.12 |
| codex_reference_mode1_frames1 | 2.47 | 9.89 | 4.01 | 0.13 | 3.84 | 30.02 |
| codex_reference_mode1_frames2 | 2.47 | 9.78 | 3.97 | 0.13 | 3.79 | 30.06 |
| codex_throughput_default | 2.72 | 9.84 | 3.62 | 0.43 | 3.94 | 9.23 |
| codex_throughput_direct_syndrome | 2.42 | 8.73 | 3.61 | 0.38 | 3.94 | 10.42 |

Geomean e2e is 3.17×, which meets its ≥3× criterion. mixed_codec simulate is 2.98×, below the ≥3× bar,
and original_codec is thin at 3.17×.

The mixed_codec simulate split is about 1.66 s run and 0.55 s setup. The kernel census reports
interpreted=0. The run is affected by:
- 7,613 deoptimizations, mostly 'U' operands in the VHDL `gf_mult` instances under `u_euclid`;
- about 2M slow-op helper calls: extract from wide registers 729k, `mem_write` 602k, dynamic
  part-select 428k;
- 240k wide `DynamicPartInsert` helper runs.

Owner, 2026-10-26: the design-level optimizations (P6 collapsing, constant propagation, dead logic;
cross-instance partitions; VHDL delta collapsing; native wide values) are now v4 scope after these
cuts.

### 2026-10-26 (late night): SystemC matrix compiler work cut (`57b96ae0`)

The SystemC matrix shards spent nearly all their time in clang child processes (about 94% of the
scheduling shard's profile). Two changes cut the compiler work:

- A process remembers each `-M` dependency scan, keyed by command and working directory. Before the
  result is reused, size and write-time stamps of the scanned headers and searched directories are
  checked. This covers `plugin_compiler_dependencies.cpp` (cache key) and `incremental_compiler.cpp`
  (input digest).
- Shards that compiled the same models into one cache per case now share caches. The interpreter
  references keep their own cache, so the compiled pairs' cold builds still miss.

| Matrix | Before | After |
|---|---:|---:|
| Plug-in compiles (`-c`), all parts | 29 | 15 |
| Dependency scans (`-M`), all parts | at least 84 | 29 |
| `scheduling` shard: compiles / scans | 10 / 54 | 3 / 5 |

Counts come from `strace -f -e execve`. Wall times were not measured because the host was heavily
loaded. Full ctest passed in normal and `FSIM_V4_STRICT=1` mode, apart from `fsim.native-path-io`,
which was then fixed (stamps go through `support::native_fs`). Commit message correction: it says
"77 scans"; the before count is at least 84.

### 2026-10-26 (night): every feature on v4; full ctest clean, strict included (sweep `v4c8`)

Owner gate (2026-10-26): every feature runs on v4 (SystemC, coverage, the
library API, the debugger), and the whole ctest passes with no known-failure
allowance.

- **SystemC on v4.**
  - SystemC processes stay with the Accellera kernel. A design whose only
    processes are SystemC keeps its static kernel, with nothing to schedule.
  - SystemC can instantiate HDL at any level: top, nested, and with HDL
    instances below it. A native module marked with
    `fsim::systemc::hdl_module` stands for an HDL instance, and its ports are
    that instance's port map (IEEE 1666-2011 4.1.3; association by name).
  - The ABI host struct gains `mark_hdl_module` and `set_hdl_module_actual`.
    The bridge is at revision 3, and diagnostics are `FSIM-BIND-066`..`068`.
  - The root kernel process waits on the channels its HDL descendants drive.
  - The SystemC hierarchy matrix covers every placement.
- **Coverage** runs as a kernel operation. The library API and the Tcl
  debugger plan kernel access through `plan_kernel_access`, with debug points
  and locals.
- **Strict mode.** `FSIM_V4_STRICT=1` throws when a process runs on the
  reference interpreter in a v4 run, or when the kernel refuses a design.
  - The full ctest passes under it.
  - A halted process is no longer counted as interpreted.
- **Kernel scheduling.**
  - Hosted and native processes that are ready in the same Active round run
    in process order (`run_merged_active`).
  - A host task of the running batch that is ahead of a ready thread runs
    first. Example: a delayed gate update maturing in the time step, as in
    `t_risefall_delay` (IEEE 1800-2023 4.7).
  - SystemVerilog late starters run from their own task (`queue_late_start`).
  - After `$finish`/`$stop`, the final blocks run and the kernel's pending
    threads are discarded.
- **LRM fixes that ctest and the sweep found.**
  - A constrained generic actual must match the formal's length
    (1076-2019 14.3.3.2; `FSIM-ELAB-GENERIC-008`).
  - An object's initial value must have its type and its subtype's length
    (14.4.2.5; new `FSIM-ELAB-VHINIT-002`).
  - Element subtypes resolve in the type's own declaration scope (12.3).
  - String literal actuals take a constrained formal's length.
  - SystemVerilog forward parameter references are rejected (1800-2017
    6.20.2), but only for a use written inside the declaring scope. A class
    parameter's actual, substituted into the class body, keeps the body's
    source.
  - The matching-case overlap rule is read strictly (1076-2019 10.9). nvc
    `issue705` and `case11` are excluded as non-LRM.
- **Elaboration-time evaluation budget.** A function evaluated at elaboration
  runs within a budget of 2^30 units, so a value too costly to fold is
  rejected rather than run indefinitely.
  - Each operation costs one unit, plus the 64-bit words it produces.
  - An operator over values wider than a word costs 8 units per bit, because
    it visits each bit.
  - A run that exhausts the budget is memoized as a failure.
  - Lowering that reports errors leaves no value.
  - nvc `elab35` (a 64 K-entry memory initializer) elaborates in 1.1 s; the
    2 M-element rejection test trips in 2 s, and `fsim_elaboration_tests`
    takes 4.7 s.
- **ctest closure.** Stale expectations and governance pins were updated.
  Examples: the 1 fs auto resolution, the XXH64 payload index
  (plug-in payloads keep their SHA-256 provenance), schema pins, and the
  removed native cache. Each update was checked against the LRM, and product
  bugs were fixed:
  - the hierarchy path table overran on a path longer than a chunk;
  - hard-linked producers were deduplicated;
  - a VHDL package reference had no resolved target;
  - an external name's subtype width was not checked;
  - BOOLEAN loop parameters were wrong in `'pos`.
  - The diagnostic catalog is 2975.

Full ctest: 1085/1085, and 1085/1085 with `FSIM_V4_STRICT=1`.

Sweep `v4c8`: the frozen binary `fsim-archive/v4c8`, `--engine v4`, against
`b96x1`.

| Suite | Pass (base) | Pass (v4) | Planned | Admitted | host=0 | Host processes | Hosted processes | interpreted=0 | Runs interpreted>0 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| ivtest | 2635 | 2640 | 2273 | 2273 | 2273 | 0 | 2592 | 2263 | 0 |
| sv-tests | 1495 | 1495 | 387 | 387 | 387 | 0 | 422 | 386 | 0 |
| verilator | 2065 | 2070 | 2087 | 2087 | 2087 | 0 | 3647 | 2072 | 0 |
| vests | 3646 | 3646 | 1606 | 1606 | 1606 | 0 | 973 | 1596 | 0 |
| nvc | 1261 | 1259 | 1208 | 1208 | 1208 | 0 | 1059 | 1148 | 0 |
| vhdl-compliance | 65 | 65 | 65 | 65 | 65 | 0 | 86 | 65 | 0 |

- **Losses against `b96x1`: 0 (G2 met).** The two nvc cases that `b96x1`
  passed, `issue705` and `case11`, are now excluded as non-LRM (strict 10.9
  reading, owner decision 2026-10-26). Every in-scope case passes in all six
  suites.
- **No run interprets (G3).** The run-time counters left are `kernel_deopts`
  (14 cases, 48 deopts) and `specialize_folding` (131 cases).
- **G4 met:** full ctest, normal and strict.
- Census: `.local-artifacts/v4-migration/census/v4c8/census.md`.

### 2026-10-26: no interpreted runs; XXH64 snapshot checksums; ctest (sweep `v4c6`)

- **Designs whose only programs are commit programs** (port conversion,
  resolvers: `nvc_issue1161`, `bounds43`) now get a kernel without members.
  Its host process is shaped from the first valid process (`host_shape`), and
  the kernel's hosted tier runs the commit programs natively. Before, the
  plan was trivial, there was no kernel, and the commit programs ran in
  `Impl::execute`.
- **Snapshot payload checksums** (owner decision 2026-10-26): `.fsimdesign`
  payloads (runtime, DesignIR, hierarchy paths, HIR, coverage, UVM) are
  checksummed with XXH64, spelled `xxh64:<16 hex>`
  (`support/payload_checksum`). The verifier and the design validator still
  accept SHA-256 hex. Library objects and source identities stay SHA-256.
  Simulate had spent 70–100 ms re-hashing about 130 MB with SHA-256.
- **Warm-tier codegen knobs** for experiments: `FSIM_STATIC_KERNEL_WARM_LEVEL`,
  `FSIM_STATIC_KERNEL_WARM_FAST_ISEL`. The hot threshold is now part of the
  codegen identity. The ahead-of-time experiments on the codec cases (CPU 9,
  three samples each):

  | Setting | mixed build | mixed simulate | original build | original simulate |
  |---|---:|---:|---:|---:|
  | default | 10.6 s | 2.32 s | 11.4 s | 2.61 s |
  | `CODEGEN_LEVEL=1` (all templates) | 34.6 s | 2.07 s | 22.9 s | 2.40 s |
  | warm level 1, `OPT_MAX_SIZE=4096` | 18.1 s | 2.04 s | 17.0 s | 2.40 s |
  | full instruction selection at O0 | 11.8 s | 2.36 s | 14.0 s | 2.57 s |
  | `instcombine` in the cold pipeline | 21.4 s | 2.32 s | 20.9 s | 2.58 s |

  Greedy register allocation is the only setting that pays, and it costs
  6–23 s of elaboration (the e2e geomean would drop below 3×). The defaults
  are unchanged.
- **Test expectation.** `vhdl_logic9`'s slice-sharing design has 641
  elaborated processes since `v4a11`. Each indexed output formal is now a
  source stub of the resolved actual plus one conversion driver for its
  element run (IEEE 1076-2008 6.5.7.1, 14.7.3.2).

Sweep `v4c6`: the frozen binary `fsim-archive/v4c6`, `--engine v4`, against
`b96x1`.

| Suite | Pass (base) | Pass (v4) | Planned | Admitted | host=0 | Host processes | Hosted processes | interpreted=0 | Runs interpreted>0 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| ivtest | 2635 | 2642 | 2273 | 2273 | 2273 | 0 | 2592 | 2263 | 0 |
| sv-tests | 1495 | 1495 | 387 | 387 | 387 | 0 | 422 | 386 | 0 |
| verilator | 2065 | 2070 | 2087 | 2087 | 2087 | 0 | 3647 | 2072 | 0 |
| vests | 3646 | 3646 | 1606 | 1606 | 1606 | 0 | 973 | 1596 | 0 |
| nvc | 1261 | 1261 | 1209 | 1209 | 1209 | 0 | 1059 | 1149 | 0 |
| vhdl-compliance | 65 | 65 | 65 | 65 | 65 | 0 | 86 | 65 | 0 |

- **Losses against `b96x1`: 0 (G2 met).**
- **No run interprets (G3).** Run-time counters left:
  - `kernel_deopts` (14 cases, 48 deopts), which resume in the operation
    tier;
  - `specialize_folding` (133 cases). That is constant folding while the
    kernel is built. The corpus elaborates with `--no-aot`, so the folding
    runs in the simulate process; ahead of time it runs at elaborate.
- **G4 met:** the labelled ctest fails only the 18 known tests
  (`.local-artifacts/v4-migration/tools/ctest-known-v4.txt`).
  `fsim.scv.artifact` (unlabelled) fails on a library metadata format
  expectation that predates this work.
- Census: `.local-artifacts/v4-migration/census/v4c6/census.md`.

### 2026-10-25 (late night, later): nothing interpreted at run time; hosted tier (sweep `v4c5`)

Toward the owner gate (no interpretation in a compiled simulation), from `v4b8`:
- **Census instrumentation.** Static `static-kernel-interpreted` rows and a
  dynamic `static-kernel-run` line after each run: `interpreted=` (kernel
  `step_generic`), `executes=` (`Impl::execute` loops), `kernel_bytecode_runs=`,
  `kernel_operations=`, `kernel_deopts=`, `constant_drivers=`. `census.py`
  reports them per suite.
- **Operation tier.** A member, thread or deopt resume that has no
  compiled body runs native step code: one `KOp::step` per SimIR operation,
  calling the helper of that operation's kind (`fsim_sk_step_<K>`,
  `fsim_sk_thread_step_<K>`), chosen when the code is generated
  (`simir_operation_kinds.hpp`). Step code is shared by content.
- **Hosted tier.** `Impl::execute` is split into `execute_operation<Op>` and
  `continue_after_boundary`. Hosted processes, commit and resolver programs
  and startup run native step code over their operation list
  (`fsim_sk_hosted_step_<K>`), built ahead of time at adoption and cached
  per process (validated by list, body identity and size).
- **Compiled-thread suspension.** Generated code stores the suspending
  operation's boundary helper (`fsim_sk_boundary_<K>`) in the native frame;
  the runner calls it instead of visiting the operation.
- **Compact constant drivers** are a fixed-shape routine, now counted as
  `constant_drivers=`.
- **Long repeat loops.** A SystemVerilog `repeat` with a constant count above
  256 is lowered as a runtime loop rather than unrolled (IEEE 1800-2017
  12.7.2). `t_randomize_real` (`repeat (1000)` around a 64-iteration `for`)
  had unrolled to 2.4M operations and a 1 GB LLVM module, and timed out on
  `v4c4`. It now simulates in 0.45 s.
- **Codegen.** Return steps share one dispatch block per body. Template keys
  hash the codegen identity once (xxh3).
- **Load.** A hierarchy-path table carries its canonical digest, so runtime
  and design artifacts bind external paths by digest and do not re-encode them.
  `Reader::read_exact` has an inline fast path.

Sweep `v4c5`: the frozen binary `fsim-archive/v4c5`, `--engine v4`, against
`b96x1` (`v4c4` lost `t_randomize_real` to a timeout; fixed above).

| Suite | Pass (base) | Pass (v4) | Planned | Admitted | host=0 | Host processes | Hosted processes | interpreted=0 | Runs interpreted>0 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| ivtest | 2635 | 2642 | 2273 | 2273 | 2273 | 0 | 2592 | 2263 | 0 |
| sv-tests | 1495 | 1495 | 387 | 387 | 387 | 0 | 422 | 386 | 0 |
| verilator | 2065 | 2070 | 2087 | 2087 | 2087 | 0 | 3647 | 2072 | 0 |
| vests | 3646 | 3646 | 1606 | 1606 | 1606 | 0 | 973 | 1596 | 0 |
| nvc | 1261 | 1261 | 1209 | 1209 | 1209 | 0 | 1059 | 1148 | 1 |
| vhdl-compliance | 65 | 65 | 65 | 65 | 65 | 0 | 86 | 65 | 0 |

- **Losses against `b96x1`: 0 (G2 met).**
- Runs that still interpret: `nvc_issue1161` (2 `Impl::execute` runs). Its only
  programs are commit programs, so the plan was trivial and no kernel ran
  them. Run-time counters elsewhere: `specialize_folding` in 133 cases (7814
  operations), `kernel_deopts` in 14 cases (48; resumed by the operation
  tier, not interpreted).
- Census: `.local-artifacts/v4-migration/census/v4c5/census.md`.

G5 quiet timings on the live build (CPU 9, single runs, ±4% build-to-build
layout noise): `mixed_codec` simulate 2.37–2.45 s (target ≤2.27 s),
`original_codec` 2.65–2.76 s (≤2.53 s). Generated code is 44% of the
`mixed_codec` profile, spread over 8035 templates.

### 2026-10-25 (late night): no host processes; port wiring; owner gate: nothing interpreted (sweep `v4b8`)

From sweep `v4b7` (`v4a9`):
- **Language of a design without native members.** The drivers of implicit
  signals (`S'STABLE`, `S'QUIET`, ...) run in the SystemVerilog scheduling
  domain but belong to the VHDL signal they observe; they now count as VHDL
  processes when the planner picks the design's language. Eight designs with
  one VHDL process beside such drivers had been planned in SystemVerilog
  mode, which left the VHDL process on the host.
- **SystemVerilog-domain processes of a VHDL kernel.** A VHDL or mixed
  kernel hosts every Active-region SystemVerilog-domain process it cannot run
  natively, not only implicit-signal drivers (`vlog1`, `issue1285`: a Verilog
  process that reads a `std_logic` port into a `logic9` register). A wake from
  the other domain runs in the next delta, one from its own domain in the
  current Active region, as on the host.
- **Trivial plans.** A design whose only processes are commit programs counts
  them as commit-program rows, like a planned kernel (`bounds43`,
  `issue1161`).
- **Mixed kernel, out-of-round SystemVerilog work.** The G5 rerun's preflight
  found a stimulus-parity mismatch on `mixed_codec` (fsim 117619 events,
  Vivado 117638). The Euclid decoder's RAM read port is a hosted VHDL process
  and its address counter a native member. An SV clock edge drained at the
  start of an activation woke the counter for the next loop iteration, in the
  same host delta, while the hosted read port, woken through the host
  fanout, ran a host delta later and read the advanced address. With a host
  boundary, the members and threads woken by SystemVerilog work outside a
  round now wait for the next host delta (`sv_drain_outside_round`;
  1076-2019 14.7.5.3, S16). New compliance case
  `mixed_vhdl_delta_after_sv_write` (fails on `v4a8`, passes now).
  `mixed_codec` had not run in the migration's campaigns before (the
  first one stopped at the VHDL compatibility flag); this bug arrived with
  hosted VHDL processes (`v4a5`), when the design was first admitted.

Since then (`v4a10`, `v4a11`):
- **Planner driver wiring.** A VHDL conversion driver or resolved-signal
  stub whose program only routes bits becomes a kernel wire, copied in the
  commit round with its target offset. The wired programs no longer count
  as host readers of their sources. Before that, `mixed_codec` reported 1201
  boundary outputs; it now reports 0.
- **Element associations to resolved actuals.** A formal associated with a
  slice or element of a resolved actual (`y => r(1 downto 0)`) is one source
  of those elements (IEEE 1076-2019 6.5.7.1, 14.7.3.2). The resolved-actual
  adapter used to accept whole actuals only. It now splits the placed routes
  into contiguous runs and makes one conversion driver per run, with the
  run's offset. Initialization inserts each run into the driver's slot.
  Before this, such ports kept a `port_result` process and resolved one delta
  late. New compliance cases: `vhdl_port_element_wiring`,
  `vhdl_port_element_resolved`, `mixed_vhdl_port_element_wiring`.
- **Runtime word paths.** `$display` formatting reads Logic4 digits word by
  word (IEEE 1800-2017 21.2.1.3 for x/z digits), with a decimal fast path up
  to 64 bits. `dynamic_part_write_value` takes the base from the known word.
  Scheduler queues swap instead of moving their contents, and the
  readiness-ticket recycler stops at the highest ticket in use.
- **Harness note.** The perf harness's `.sim` files had passed
  `+PERF_PREFLIGHT`, which the campaign's timed samples do not. The `1008d`
  numbers cannot be reproduced: the pre-LRM build recompiles kernel native
  code at simulate, and `1008d` ran the legacy JIT for non-kernel processes.

Sweep `v4b8`: the frozen binary `fsim-archive/v4a11`, `--engine v4`, against
`b96x1`.

| Suite | Pass (base) | Pass (v4) | Planned | Admitted | host=0 | Host processes | Hosted processes |
|---|---:|---:|---:|---:|---:|---:|---:|
| ivtest | 2635 | 2642 | 2273 | 2273 | 2273 | 0 | 2592 |
| sv-tests | 1495 | 1495 | 387 | 387 | 387 | 0 | 422 |
| verilator | 2065 | 2070 | 2087 | 2087 | 2087 | 0 | 3647 |
| vests | 3646 | 3646 | 1606 | 1606 | 1606 | 0 | 973 |
| nvc | 1261 | 1261 | 1209 | 1209 | 1209 | 0 | 1059 |
| vhdl-compliance | 65 | 65 | 65 | 65 | 65 | 0 | 86 |

- **Losses against `b96x1`: 0 (G2 met).**
- **Host processes: 0 in every suite.** Commit programs (port conversion,
  resolvers, inout adapters) are counted as kernel-owned rows. They still
  run inside the host's update commit.
- Census: `.local-artifacts/v4-migration/census/v4b8/census.md`.

Campaign `v4-mig-1025f` (`v4a11`, `v4_o2_aot`, CPU 9, three paired samples;
all 40 pair verdicts pass):

| Case | fsim e2e | xsim e2e | e2e ratio | fsim simulate | xsim simulate | simulate ratio | fsim elaborate |
|---|---:|---:|---:|---:|---:|---:|---:|
| codex_reference_mode0_frames1 | 2.47 s | 9.99 s | 4.05x | 0.13 s | 4.04 s | 31.54x | 1.23 s |
| codex_reference_mode0_frames2 | 2.32 s | 9.38 s | 4.05x | 0.13 s | 3.84 s | 30.07x | 1.18 s |
| codex_reference_mode1_frames1 | 2.47 s | 9.84 s | 3.99x | 0.13 s | 3.84 s | 30.01x | 1.28 s |
| codex_reference_mode1_frames2 | 2.47 s | 9.89 s | 4.01x | 0.13 s | 3.84 s | 30.41x | 1.28 s |
| codex_throughput_default | 2.82 s | 10.19 s | 3.61x | 0.48 s | 3.99 s | 8.33x | 1.28 s |
| codex_throughput_direct_syndrome | 2.62 s | 9.24 s | 3.52x | 0.43 s | 4.09 s | 9.52x | 1.03 s |
| mixed_codec | 13.36 s | 26.64 s | 1.99x | 2.39 s | 6.80 s | 2.85x | 10.26 s |
| mixed_throughput | 5.28 s | 13.45 s | 2.55x | 1.78 s | 6.25 s | 3.51x | 2.84 s |
| original_codec | 16.98 s | 28.81 s | 1.70x | 2.69 s | 7.60 s | 2.83x | 14.02 s |
| original_throughput | 5.91 s | 16.28 s | 2.76x | 1.83 s | 7.60 s | 4.14x | 3.79 s |

Geomean end-to-end ratio: 3.09x

G5a holds for the geomean (3.09×) but not for every simulate: `mixed_codec`
(2.85×) and `original_codec` (2.83×) still need about 6% and 9%.
Elaboration of the codec designs (10-14 s) is the G5b concern.

**Owner direction (2026-10-25): the interpreter is never invoked in a
compiled simulation.** This is strict. Nothing dispatches over SimIR
operations or kernel instructions at run time:
- no `Impl::execute`, which covers hosted processes, commit and resolver
  programs, and startup;
- no kernel `step_generic`, `behavioral_step` or generic instructions;
- no kernel bytecode members;
- no helper that switches over kernel instructions.

Native code may call a dedicated helper per operation. G3 is now
"interpreted sites 0", which implies host=0 and hosted=0. The plan is in
`.local-artifacts/v4-migration/hosted-plan.md`. Its first step is
instrumentation: static `static-kernel-interpreted` rows, and a dynamic
`static-kernel-run interpreted=` line after each run.

### 2026-10-25 (night): legacy JIT gone, debugger on the kernel, perf regressions fixed (sweep `v4b6`)

The legacy per-process LLVM JIT and its runtime are deleted, and the kernel
runs every SystemVerilog and VHDL client.
- **Retirement bundles 1-3.** Bundles 1-2 removed the JIT code (non-test and
  tests). Bundle 3 removed the runtime machinery that only the JIT reached:
  - the native frontier, the A4 signal state and the region-graph runtime;
  - region activation and programs, the fused static graph and the owned
    drivers;
  - the scheduler frontier-batch API.
  `RegionGraph` keeps only the build and the access and alias analyses.
  Signal driver inventories moved to `simir_signal_driver_inventory.cpp`, and
  the surviving observation barriers to `simir_observation.cpp`.
- **Debugger on the kernel.** `fsim debug` and every Tcl session now plan the
  kernel's access the same way as the run command (`plan_kernel_access`):
  foreign and observing, and the debugger also gets stoppable threads. Before
  this, the Tcl path never set an access plan, so the debugger ran on the
  interpreter. `FSIM_DEBUG_KERNEL=0` still selects the interpreter. The SV and
  VHDL debugger scripts produce identical transcripts on both (breakpoints,
  step, frame, signal breakpoints, inspect).
- **Simulate regressions found by the G5 campaign.**
  - **`fork_callable_frames` (M1) was too broad.** It is now
    `fork_holds_suspended_frame`: a process is refused only when a fork child
    can enter an automatic callable that may suspend, or when a join_any or
    join_none fork coexists with such a callable (IEEE 1800-2023 9.3.2,
    13.4.2).
  - **The sole-writer partition rule (M1) ignored bit ranges.** Writers of
    disjoint bits of one vector now stay partitioned (one continuous
    assignment per bit, as in `gf_mult`).
  - **The cycle exclusion (M1) was too broad.** It applies only to a cycle
    with a member that assigns one signal more than once per run.
  - **`CopyContainerRegister`.** The whole-array copy that B81 lowering emits
    is supported in behavioral members (`KOp::mem_copy`).
- **Kernel correctness.**
  - Private containers of the member in the host stub's slot.
  - Final blocks see the values a `$finish`-ed activation staged (the end of
    simulation commits every output at once).
  - Behavioral waiters on a part-select wake only when the selected bits
    change (9.4.2).
- **Audits.** The diagnostic codes only the JIT emitted (FSIM-AOT-002,
  FSIM-CACHE-0004, FSIM-COV-011) are retired. Inventory anchors and pins
  follow the moved code, and the closure witness `fsim.llvm` is replaced by
  `fsim.application.static-kernel`.

| Case (simulate, CPU 9) | v4a1 | v4a7 | v4a8 |
|---|---:|---:|---:|
| original_throughput | 2.08 s | 15.3 s | 2.4 s |
| original_codec | >250 s | 120 s | 3.1 s |

Labelled ctest: 18 failures, all known (`tools/ctest-known-v4.txt`). The
`.compiled` names on the B96 list are now `.v4`, and five entries named
deleted tests.

Sweep `v4b6`: the frozen binary `fsim-archive/v4a8`, `--engine v4`, against
`b96x1`.

| Suite | Pass (base) | Pass (v4) | Planned | Admitted | host=0 | Host processes | Hosted processes |
|---|---:|---:|---:|---:|---:|---:|---:|
| ivtest | 2635 | 2642 | 2273 | 2273 | 2273 | 0 | 2592 |
| sv-tests | 1495 | 1495 | 387 | 387 | 387 | 0 | 422 |
| verilator | 2065 | 2070 | 2087 | 2087 | 2087 | 0 | 3647 |
| vests | 3646 | 3646 | 1606 | 1606 | 1605 | 1 | 980 |
| nvc | 1261 | 1261 | 1209 | 1209 | 1198 | 12 | 1058 |
| vhdl-compliance | 65 | 65 | 65 | 65 | 65 | 0 | 86 |

- **Losses against `b96x1`: 0 (G2 met).** Every planned simulate admits the
  kernel; no design-level refusal remains.
- **Host processes (G3 open, 12 cases).**
  - Eight designs have no native member and one VHDL process beside
    implicit-signal drivers (SystemVerilog scheduling domain). The planner
    counts the drivers as SystemVerilog processes and plans the design in
    SystemVerilog mode, which leaves the VHDL process on the host.
  - Two designs (`bounds43`, `issue1161`) have only port-conversion
    programs; the trivial plan counts them as host processes.
  - Two mixed designs (`vlog1`, `issue1285`) have a Verilog process with
    non-`logic` registers in a VHDL kernel.

### 2026-10-25 (later): M1 — no losses on `--engine v4` (sweep `v4b3`)

`--engine v4` selects the static kernel with no per-process JIT. Every
gate sweep now runs on it; the runner and the suite tools take
`--engine v4`/`ENGINE=v4`. The planner logs a census under
`FSIM_PROFILE_PHASES`, one `static-kernel-host reason=` row per reason that
keeps processes on the host. The kernel-on fixes:
- **Package initializers.** Package static initializers run before the
  kernel's first activation.
- **Dynamic indices.** Out-of-range dynamic indices follow the LRM default
  values.
- **Behavioral divergences.**
  - Pause, Report and string operations in the behavioral tier.
  - Access completeness checks.
  - Gold-file output ordering.
- **VHDL behavioral tier.** VHDL processes may run as behavioral members
  (`FSIM_STATIC_KERNEL_VHDL_BEHAVIORAL=0` turns this off).
- **Kernel NBA commit.** While the host still has Active or Inactive work in
  the slot, the kernel's NBA commit waits for a host update-region task
  (IEEE 1800-2023 4.4.2.4); inside that task kernel updates commit at once.
- **Levelization.**
  - Combinational members are ordered by every signal they read, not only
    by their sensitivity.
  - Writers of one signal are ordered by process id.
- **Host inputs.**
  - A host signal whose only writer is one kernel member is mirrored into
    the kernel when the write is staged.
  - Designs with force/release do not use this mirror.

Sweep `v4b3`: the frozen binary `fsim-archive/v4a3`, `--engine v4`, against
`b96x1`. In the table below, "Pass (base)" is the `b96x1` count.

| Suite | Pass (base) | Pass (v4) | Planned | Admitted | host=0 | no_members | vhdl_partial | unknown_dependencies | gate | other refusals | Host processes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| ivtest | 2635 | 2640 | 2273 | 1738 | 1367 | 321 | 0 | 102 | 79 | 33 | 3343 |
| sv-tests | 1495 | 1495 | 387 | 234 | 130 | 77 | 0 | 74 | 0 | 2 | 684 |
| verilator | 2065 | 2071 | 2087 | 1508 | 1032 | 35 | 0 | 484 | 54 | 6 | 4929 |
| vests | 3646 | 3646 | 1606 | 937 | 937 | 507 | 98 | 11 | 0 | 53 | 4838 |
| nvc | 1261 | 1261 | 1209 | 646 | 578 | 372 | 145 | 5 | 29 | 12 | 2974 |
| vhdl-compliance | 65 | 65 | 65 | 0 | 0 | 59 | 6 | 0 | 0 | 0 | 96 |

- **Losses against `b96x1`: 0 (M1 met).**
- **Top host reasons.**

  | Reason | Cases | Processes |
  |---|---:|---:|
  | `opaque_access:ClassAllocate` | 587 | 633 |
  | `behavioral_string_register` | 563 | 726 |
  | `design_unknown_dependencies` | 442 | 1139 |
  | `SystemVerilogScalarBinary` | 339 | 598 |
  | `behavioral_not_initialized` | 290 | 2796 |
  | `shape_vhdl_once` | 201 | 317 |
  | `design_vhdl_partial` | 162 | 3127 |

- **Next (M2/M3).**
  - Design: processes the kernel cannot run natively become "hosted
    members". The kernel schedules them in its own regions and runs them
    with the reference operation semantics.
  - Opaque processes stop refusing the design: the kernel publishes every
    owned signal and imports host writes to it.
  - Coverage, trace, foreign-library, specify and `vcd_control` stop being
    gates; an access plan takes their place.

### 2026-10-25: resumption after the LRM closure; corpus census

The migration resumes after the LRM closure (Batch 96, `d7dfa257`). The owner's
direction:
- the legacy per-process compiled engine no longer needs to work;
- at the end, the whole six-suite LRM corpus runs on v4.

The baseline census ran the closure binary (`6803a2de`) over the corpus with
`FSIM_STATIC_KERNEL=1`, on the runner's default `--engine interpreter`. Kernel
members run as native code; the remaining processes run on the reference
evaluator.

| Suite | Pass, kernel off | Pass, kernel on | Kernel planned | Admitted | no_members | vhdl_partial | unknown_dependencies |
|---|---:|---:|---:|---:|---:|---:|---:|
| ivtest | 2635 | 2567 | 2194 | 1723 | 215 | 0 | 223 |
| sv-tests | 1495 | 1495 | 387 | 225 | 40 | 0 | 120 |
| verilator | 2065 | 2052 | 2033 | 1290 | 82 | 0 | 655 |
| vests | 3646 | 3646 | 1606 | 24 | 1244 | 164 | 174 |
| nvc | 1261 | 1259 | 1180 | 219 | 689 | 199 | 73 |
| vhdl-compliance | 65 | 65 | 65 | 0 | 0 | 0 | 65 |

- **Admission.** 47% of the planned cases admit the kernel; "admitted" still
  leaves some processes on the host.
- **Losses.** Switching the kernel on loses 97 passing cases. The largest
  group is 37 package static initializer failures; the rest are behavioral
  divergences (31), dynamic-index errors (7), force/release and `$stop`
  cases (13), and singles.
- **Gaps.** VHDL has no behavioral tier, so VHDL testbenches are refused
  (`no_members`, `vhdl_partial`). `unknown_dependencies` refuses 1310 cases.
- **Plan of record.** The resumption plan sets milestones M0–M4: plumbing and
  census, kernel-on correctness, admission, full ownership, then switch and
  retirement. Gates G1–G5 are the definition of done. The plan is in the
  local handoff `.local-artifacts/v4-migration/HANDOFF.md`.
- **Owner decisions (2026-10-25).**
  - Every process runs inside the v4 engine; no host processes remain.
  - `--engine compiled` becomes an alias of v4.
  - Excluded race cases stay excluded, even where the kernel happens to pass
    them.
  - Gate sweeps run on `--engine v4`; the interpreter sweep becomes a
    periodic oracle check.

### 2026-10-08 (later): exit criteria met — campaign `v4-corpus-1008d`

Candidate 847d6c56, frozen as `v4cand-corpus-1008d`. Configuration `llvm_o2_aot`
(elaborate `--aot --aot-scope all`), 3 samples, CPU 9, xsim measured in the same session.
All 30 pair verdicts passed; the correctness lines are identical to xsim's.

| Case | xsim total | xsim simulate | fsim total | fsim simulate | e2e | Simulate ratio |
|---|---:|---:|---:|---:|---:|---:|
| original_codec | 29.66 | 7.49 | 12.20 | 2.48 | 2.43× | **3.02×** |
| original_throughput | 15.85 | 7.44 | 4.88 | 1.63 | 3.25× | 4.58× |
| mixed_codec | 24.96 | 6.54 | 10.52 | 2.08 | 2.37× | **3.15×** |
| mixed_throughput | 12.52 | 5.99 | 4.14 | 1.42 | 3.02× | 4.20× |
| codex_reference_mode0_frames1 | 9.09 | 3.83 | 2.53 | 0.12 | 3.60× | 32.8× |
| codex_reference_mode0_frames2 | 9.29 | 3.83 | 2.58 | 0.12 | 3.61× | 32.8× |
| codex_reference_mode1_frames1 | 9.79 | 3.83 | 2.63 | 0.12 | 3.73× | 32.8× |
| codex_reference_mode1_frames2 | 9.89 | 3.83 | 2.63 | 0.12 | 3.77× | 32.8× |
| codex_throughput_default | 10.25 | 4.03 | 2.83 | 0.27 | 3.63× | 15.1× |
| codex_throughput_direct_syndrome | 8.79 | 3.98 | 2.48 | 0.22 | 3.55× | 18.3× |

**Both exit criteria (plan §9 D6, 2026-10-08) are met:**
- the geometric mean of the e2e speedups is **3.25×** (≥3×);
- every case's simulate phase is **≥3×** faster than `xsim -R`.

**Margins are thin.** original_codec simulate passes at 3.02×, and the machine's speed
drifts about 5% over hours (`1008c` measured 2.94× for nearly the same code).
mixed_codec has 5% headroom.

### 2026-10-08 (later): plain stores for silent slots in templates

**What changed.** Canonicalization marks a narrow `store_slot` to a silent slot
(`aux = 1`). The mark is part of the template's shape, so units whose slot is not silent
get their own template. Codegen emits a plain store for these: no old-value load, no
comparison, no notification. Before, the silent bit came only from the bindings at run
time, so even the hot tier kept the comparison and both branches for every
partition-internal net.

**Results**, interleaved A/B against the 1008c binary, output identical:
- original_codec: 2.63 → 2.53 s.
- original_throughput: 1.76 → 1.69 s.

**Not kept: a higher tier for clocked templates.**
- Giving all of them the hot tier cuts original_codec by 0.14 s, but costs +3.2 s of
  elaborate there and +7.9 s on mixed_codec: the geomean would drop to about 2.8×.
- A units × size weight gains only 0.03 s.

### 2026-10-08 (later): confirming AOT campaign `v4-corpus-1008c`

Candidate 01590e32, `llvm_o2_aot`, 3 samples, CPU 9, xsim measured in the same session.
The machine ran slower than during `1008b`: xsim original_codec simulate 7.59 s against
7.34 s.

| Case | xsim simulate | fsim simulate | Simulate ratio | e2e |
|---|---:|---:|---:|---:|
| original_codec | 7.59 | 2.58 | **2.94×** (needs ≤2.53 s) | 2.11× |
| mixed_codec | 6.59 | 2.13 | 3.10× | 2.32× |
| mixed_throughput | 6.14 | 1.48 | 4.16× | 3.01× |
| original_throughput | 7.65 | 1.78 | 4.31× | 3.26× |
| codex (six) | 3.88–4.08 | 0.12–0.27 | 15–34× | 3.55–3.81× |

- **Geometric mean e2e: 3.23×.**
- **Simulate ≥3×:** nine of ten cases. original_codec misses by 0.05 s.

### 2026-10-08 (later): persisted canonical templates; partition store forwarding

- **Canonical templates and per-unit bindings are persisted** (kind `native`, keyed
  by the canonicalization build).
  - `build_native` skips canonicalization and grouping when a recorded image fits: the
    unit count, the template indices and each template's code length must match.
  - Native stage: original_codec 70 → 26 ms.
  - Interleaved A/B against the campaign binary: original_codec 2.61 → 2.55 s,
    mixed_codec 2.19 → 2.10 s.
- **Partition store-to-load forwarding.** A load of a whole narrow slot that a
  branch-free member of the same partition stored earlier in the pass becomes a register
  copy; the store stays. 15,000 loads in original_codec, about −0.03 s.
- **Smaller changes:**
  - The VHDL commit's per-slot record is 16 bytes. Prior values always go to
    `round_before_`.
  - Restored specializations are moved, not copied.
  - The body-image varint decoder skips per-byte bounds checks when it can.
  - `from_aval_bval` masks inline words directly.

Parity is identical on all ten cases, and ctest matches the baseline.

### 2026-10-08 (later): confirming AOT campaign `v4-corpus-1008b`

- Candidate a37af095, frozen as `v4cand-corpus-1008b`.
- Configuration `llvm_o2_aot`: elaborate `--aot --aot-scope all`, simulate
  `--compiled-processes all`.
- 3 samples, CPU 9, xsim measured in the same session.
- All runs pass their correctness patterns.

| Case | xsim total | xsim simulate | fsim total | fsim simulate | e2e | simulate |
|---|---:|---:|---:|---:|---:|---:|
| original_codec | 28.04 | 7.34 | 12.50 | 2.53 | 2.24× | **2.90×** |
| original_throughput | 15.70 | 7.44 | 4.98 | 1.78 | 3.16× | 4.19× |
| mixed_codec | 24.19 | 6.38 | 10.46 | 2.18 | 2.31× | **2.93×** |
| mixed_throughput | 13.53 | 6.24 | 4.45 | 1.53 | 3.04× | 4.09× |
| codex (six cases) | 8.95–10.25 | 3.88–3.98 | 2.48–2.83 | 0.12–0.27 | 3.56–3.81× | 15–34× |

- **Geometric mean e2e: 3.23×** (criterion met).
- **Simulate ≥3×:** eight of ten cases.
  - mixed_codec needs ≤2.13 s.
  - original_codec needs ≤2.45 s.

### 2026-10-08 (later): more codegen effort ahead of time

Code built by `elaborate --aot` puts its compile time in elaborate, where the e2e
geometric mean has room. Two new defaults apply only when building ahead of time:
- cold templates also run `simplifycfg`;
- hot (partition) templates use the full instruction selector.

Code compiled while simulating is unchanged. The cache identity covers the knobs, not the
mode, so AOT objects are found at simulate.

original_codec, paired A/B on the same binary (old settings through a wrapper):
simulate 2.80 → 2.65 s, elaborate +0.57 s.

**Rejected:** a lower hot threshold (−0.03 s for +0.5 s).

### 2026-10-08 (later): persisted specializations and compiled member bodies

`elaborate --aot` now also stores two more results beside the kernel plan, and
`simulate` restores them:
- **VHDL specializations** (`StaticKernelSpecializations`): each member's appended
  specialized operations and origins, or "not specialized". The bytes are written with
  the artifact codec's operation encoder.
- **Compiled member bodies** (KIR): varint-encoded field by field, about 30 MB for
  mixed_codec; a direct struct copy was 135 MB.

How restore is guarded:
- A body is restored only if its member's process and operation count match.
- Keys cover the artifact digest and the planner, specializer and compiler builds.
- `FSIM_STATIC_KERNEL_PLAN_CACHE=<plan|specializations|bodies>` skips one kind;
  `=0` skips all three.
- `FSIM_KERNEL_VERIFY_BODIES=1` also compiles each restored member and reports any
  field that differs.

mixed_codec compile stage: 302 → 66 ms.

**A latent interpreter bug, now fixed.** `execute_body` treated `load_slot9`'s slot index
as a register (`registers[inst.x]`). The value was unused, but the read could fall
outside the register file. With the new allocation layout it segfaulted on mixed_codec;
valgrind showed the invalid read also happened before this change.

Parity is identical on all ten cases, and ctest matches the baseline.

| Case | Simulate | Target |
|---|---:|---:|
| mixed_codec | 2.16 s | 2.25 s (**met**) |
| original_codec | 2.66 s | 2.58 s |
| original_throughput | 1.87 s | 2.62 s |
| mixed_throughput | 1.48 s | 2.10 s |

The machine ran faster during this measurement than in earlier ones.

### 2026-10-08 (later): trusted artifact load, persisted kernel plan

**Owner decision (2026-10-08).** `fsim simulate` trusts a checksummed artifact from a
matching build. It does not re-derive and cross-check it:
- The serialized driver inventory is adopted. Before, it was recomputed through a full
  region-graph build and compared.
- The DesignIR validity and runtime-projection checks are skipped.
- `FSIM_VERIFY_ARTIFACT=1` restores the checks (`elaboration::detail::TrustedArtifactLoad`).
- Checksum verification itself is unchanged.

**The static-kernel plan is persisted.**
- `elaborate --aot` stores the plan in the workspace's `static-kernel` object cache. Its
  key covers the artifact's content digest, the planner build and the plan's environment
  knobs.
- `simulate` restores it instead of planning: original_codec planning goes from 275 ms
  to 10 ms. With the inventory check gone, simulate no longer builds the region graph.
- A plan that does not fit the design (formats or references out of range) is
  discarded, and the planner runs. `FSIM_STATIC_KERNEL_PLAN_CACHE=0` always plans.

Parity is identical on all ten cases, and ctest matches the baseline.

| Case | Simulate | Target |
|---|---:|---:|
| original_codec | 2.85 s | 2.58 s |
| mixed_codec | 2.43 s | 2.25 s |
| original_throughput | 2.00 s | 2.62 s |
| mixed_throughput | 1.59 s | 2.10 s |

### 2026-10-08 (later): VHDL commit skips unchanged Logic9 writes

- **The VHDL commit's unchanged-write skip now covers 4-plane (Logic9) slots.**
  `std_logic` signals are Logic9, so the skip never fired in mixed designs before.
  - It compares the planes `store_planes_word` would write.
  - mixed_codec, interleaved A/B with frozen binaries: 2.68 → 2.56 s.
- **The fast member run caches the register-file pointer** in its `FastRun` entry. This
  removes one dependent cache miss per run. Interleaved A/B: 2.69 → 2.65 s.
- **Not kept:** prefetching the next members' run entries and register files (2.64 vs
  2.66 s).

Parity is identical on all ten cases, and ctest matches the baseline.

| Case | Simulate | Target |
|---|---:|---:|
| mixed_codec | 2.53 s | 2.25 s |
| mixed_throughput | 1.61 s | 2.10 s |
| original_codec | 3.27 s | 2.58 s |
| original_throughput | 2.14 s | 2.62 s |

### 2026-10-08 (later): report output buffering; replay measures like the campaign

- **The replay tool overstated simulate.** `r37-analysis/tools/replay.py` captured output
  through a pipe to Python. `perf_campaign.py` writes it to files. The tool now does the
  same: mixed_codec read 2.90 s through the pipe and 2.66 s with a file.
- **VHDL reports flushed standard output twice per report.** mixed_codec prints 11k
  numeric_std metavalue warnings, which cost 11k `write` calls. They now flush only when
  standard output is a terminal. Reports and `$display` share stdio's buffer, so their
  order is unchanged. Write calls: 11,204 → 580. Output is byte-identical.
- **A failed check (integer range, assertion) in two-state code** now hands over to the
  full code too.

Simulate phase, writing output to files (as the campaign does):

| Case | Simulate | Target |
|---|---:|---:|
| original_codec | 3.16 s | 2.58 s |
| original_throughput | 2.07 s | 2.62 s (**met**) |
| mixed_codec | 2.59 s | 2.25 s |
| mixed_throughput | 1.74 s | 2.10 s (**met**) |
| codex cases | ≤0.26 s | **met** |

The estimated e2e geometric mean (AOT) is about 3.5×.

### 2026-10-08 (later): two-state code hands slow paths to the full code

**What changed.** In two-state VHDL code, a value that is not all 0/1 no longer goes
through a slow path that calls the evaluate helper and merges back. Instead the full
code takes over at that instruction (frame status 4, `reserved` = the instruction). This
applies to Logic9 slot reads (`load_slot9`), exact field reads (`load_field9`) and every
`with_fallback` fallback such as integer overflow.
- This is safe because the instruction has no effects before its fallback, and every
  earlier result is already in the register file.
- The fast path now continues in its own block, with no merge or phis. At the cold tier
  that block boundary was the cost.
- **Exception:** container reads of elements outside the arena take their slow path
  every time, so they keep it.
- Hand-overs are rare: about 16k per mixed_codec run, all of them reads of X or 'U'
  values. `FSIM_PROFILE_KERNEL` counts them by instruction.

Results:

| Case | Simulate before | Simulate after | Target |
|---|---:|---:|---:|
| mixed_throughput | 2.15 s | 2.03 s | 2.10 s (**met**) |
| mixed_codec | 3.23 s | 2.86 s | 2.25 s |

### 2026-10-08 (later): setup costs at load and kernel construction

Three setup costs that grew with instance count:
- **Kernel compile cleared dense flags over all slots.** For each member it zeroed
  `slots_.size()` and `families_.size()` arrays (47k slots × 100k members on
  original_codec). These are now sorted lists of the slots and families the member
  writes. Compile stage: 145 → 96 ms.
- **Signal values were decoded bit by bit** (`PackedLogic4::from_msb_string`, one
  plane-storage write per bit). They are now decoded a word at a time.
- **The string intern pool's shards were `unordered_map<string, weak_ptr>`.** Each call
  hashed twice and copied the key. They are now open-addressed tables of (hash, string)
  that reuse stored hashes when they grow. Interning restores about 100k hierarchy paths
  at load.

original_codec simulate is 3.21 s (from about 3.40). The setup that remains is spread
over many per-instance analyses of 30–120 ms each:

| Item | original_codec |
|---|---|
| Region-graph program analyses at load (needed by the driver-inventory check and the plan) | ≈0.20 s |
| Runtime program decode | ≈0.30 s |
| Kernel construction | ≈0.39 s |
| Host population | ≈0.15 s |
| Kernel plan | ≈0.08 s |

The run loop is real work: 10.7M native runs, 15M change notifications and 25M
schedules. Slots with nothing to notify are already silent.

### 2026-10-08 (later): generated-IR quality at the cold tier

Most template code is compiled at the cold tier (O0, fast instruction selection, fast
register allocator). The emitted IR was examined against what LLVM's passes remove
(mixed_throughput's cold module: 455k IR instructions for 50k KIR instructions):
- About 28% of the IR is GEPs, 15% loads and 14% stores.
- The run-time cost is in block structure. The fast allocator spills every live value at
  each block boundary, and the emitter made blocks it did not need:
  - an X-check block before each branch on a known condition (`br i1 false`);
  - a slow-path diamond for fast paths that are always taken;
  - deopt checks on operands without 'U' bits.
- Register-file loads are rare (6.5k), so the per-block value cache works. Register-file
  stores are many (38.6k): VHDL deoptimization needs them.

mixed_codec, AOT (run = interpreter time; codegen = both AOT groups):

| Variant | Run | Codegen |
|---|---:|---:|
| Before | 2.33 s | 4.39 s |
| Codegen O1 (fast instruction selection) | 1.76 s | 25.3 s |
| Codegen O1 with the fast register allocator | 1.74 s | 23.5 s |
| O0 with the greedy allocator | 1.97 s | 15.6 s |
| Pipeline `sroa,simplifycfg` | 2.18 s | 4.76 s |
| Pipeline `sroa,early-cse<memssa>,simplifycfg` | 2.16 s | 5.52 s |
| Pipeline `sroa,instcombine,simplifycfg` | 1.95 s | 15.1 s |
| **Emitter folds constant conditions (kept)** | **2.19 s** | **3.97 s** |
| Folding plus `simplifycfg` | 2.10 s | 4.40 s |

- **Register allocation is not O1's lever.** O1 with the fast allocator keeps the whole
  gain, so the gain comes from the other machine passes.
- **The emitter now folds conditions the builder proves constant.** A branch on a known
  value has no X check. A `with_fallback` whose fast path always holds has no slow path.
  A deopt or 'U' guard on operands without 'U' bits is dropped. This is faster to run
  and faster to compile.
- **Not taken:** removing the write-queue capacity check per deferred store; the check
  stays.
- **The VHDL commit skips writes that don't change the slot.** This applies to an
  untouched slot (two planes, one word): there is no prior-value copy and no change check.
  `commit_round`'s slot-touch time halved in the profile. The gain is within the noise on
  mixed_throughput.
- **Wide part inserts no longer go through the reference path.** A narrow, known field
  written into a wide Logic4 register at a dynamic index (`DynamicPartInsert`, 256- and
  264-bit targets in mixed_codec, about 240k per run) is written in place. Before, the
  reference path copied the whole value twice. mixed_codec simulate: 3.24 → 3.17 s.
- `FSIM_STATIC_KERNEL_LLVM_ARGS` (diagnostic) passes LLVM options. `DUMP_IR` now names
  each function's template index. The perf map also covers code loaded from the AOT
  cache.

### 2026-10-08: new exit criteria, static-kernel AOT, elaboration memos

**Exit criteria (owner, plan §9 D6).**
- The geometric mean of the per-case e2e speedups over xsim must be at least 3×.
- In every case, `fsim simulate` must be at least 3× faster than `xsim -R`.
- Elaborate may build the kernel's native code (`--aot`).
- On `v4-corpus-1008a` the geometric mean was 3.63×. The simulate-phase ratios were:

  | Case | Simulate-phase ratio |
  |---|---:|
  | mixed_codec | 1.39× |
  | original_codec | 1.54× |
  | mixed_throughput | 2.30× |
  | original_throughput | 2.98× |
  | codex | 8.7–18.6× |

**Static kernel AOT.**
- `fsim elaborate --aot` now sets up a simulation without trace or coverage. The kernel
  is built inside it (`StaticKernelAheadOfTimeScope`), and every template is compiled,
  including lazy and full-code templates.
- Objects go to `<cache>/static-kernel`, one per tier group. They are stored with an
  index named by the keys of all the kernel's templates. Each template key covers:
  - its body, tier and form;
  - the resume points its full code continues at;
  - the code generator's identity: the build stamp, LLVM version, host CPU and
    features, and the codegen environment.
- Simulate looks up the same templates (`StaticKernelCodegen::available`). It links the
  objects with JITLink, restores two-state resume points, and runs templates natively
  from the start.
- Generated code calls the runtime helpers by name (`fsim_sk_*`, defined with
  `absoluteSymbols`) instead of by address.
- Measured simulate phase:

  | Case | Before AOT | With AOT |
  |---|---:|---:|
  | mixed_throughput | 2.73 s | 2.22 s |
  | mixed_codec | 4.84 s | 3.6 s |
  | original_codec | 5.04 s | 3.6 s |

  AOT elaborate costs +1.1 s on mixed_throughput, about +5.5 s on mixed_codec and about
  +5.4 s on original_codec. That includes the artifact reload, kernel setup and
  compiling every template.
- Outputs match the frozen ones.

**Elaboration.**
- **Type-mark lookups.** `effective_vhdl_subtype` skips name resolution for a type mark
  that has no type, subtype or generic-type declaration in the design, such as a
  predefined or IEEE type. This uses `vhdl_type_declarations_named`. Saves 0.26 s on
  mixed_codec.
- **Design-wide call memo.** It is shared through `VhdlInitializerMemoContext` and
  covers:
  - pure functions of non-generic packages, in both the typed and the integral
    evaluator, keyed by callable and argument values, with the work replayed;
  - the callable declaration a call expression names, for units whose actuals are all
    generic constants;
  - per evaluator, the value declaration a name resolves to.

  mixed_codec elaborate went from 4.3 s to 3.5 s and mixed_throughput from 1.39 s to
  1.24 s. The elaborated artifacts are byte-identical.
- **Codegen.** Binding words and slot pointers are hoisted into the entry block: 6% less
  codegen time.

**Runtime state.**
- Packed memories of one narrow Logic4 width are written as two dense bit planes
  (runtime-state schema 77).
- runtime.bin went from 88 MB to 66 MB on original_codec. Decode got 0.04–0.06 s faster.

**Rejected.**
- Word-plane encoding of every packed value: runtime.bin grew to 132 MB, and decode was
  barely faster.
- Compiling cold templates at LLVM Default: it crashes, and codegen took 23 s on
  mixed_codec.
- Lowering the hot threshold under AOT: no run-time gain.

**Where mixed_codec's simulate phase goes, with AOT (3.6 s):**

| Item | Time |
|---|---:|
| Load (decode 0.28 s) | 0.42 s |
| Kernel setup (plan 0.05, members 0.06, KIR and member specialization 0.31, native link 0.09) | 0.58 s |
| Interpreter start (time-zero runs, mostly on the reference evaluator) | 0.2 s |
| Run | 2.28 s |

Within the run, generated code is 53%, `commit_round` about 11% and scheduling about
7%. The rest is generic operations and the testbench threads. The owner chose to do
the architecture work this needs: fusing VHDL combinational members with exact
delta-depth publication, a persisted kernel image, and load and commit cuts.

### 2026-10-07: corpus (codec elaboration, testbench threads in the kernel)

The corpus run above showed where the codec cases spend their time. For
original_codec (30.9 s against 27.6 s for xsim):
- **Elaborate: 13.5 s.** A third of it was constant folding during expression
  lowering: 54 k fold attempts at about 110 µs each.
- **Simulate: 16.8 s.**
  - 7.0 s waits for the old JIT, which compiles the 11 testbench `initial` processes
    (121 k operations at O2) with a cold cache.
  - The kernel takes 4.7 s.
  - The host runs the testbench for about 2.9 s.

Changes:
- **Constant evaluation caches** (`specialization_cache.hpp`).
  - Each constant name's declaration is memoized per specialization and expression
    record.
  - Each declaration's value is cached. A unit-scope declaration whose evaluation reads
    no hierarchy identity is shared with derived generate-occurrence specializations
    (`derive_occurrence_specialization`). Values that read one stay per
    specialization, and reading such a cached value marks the outer evaluation too.
  - Specializations in one overlay class, when their overlay determines them, share
    all constant results. `overlay_class` now runs as soon as an instance's
    specialization exists.
  - The trigger: `GEN = gen_poly(PAR)` in `rs_encoder_top` was evaluated once per
    generate tap (145 times, 20 ms each) for only 8 distinct parameter sets.
  - Result: folding went from 6.0 s to 0.2 s, and original_codec elaboration from
    13.5 s to 6.6 s.
- **Private arrays in behavioral members.**
  - The planner admits `ContainerWrite` when its register is a fixed packed array
    that no `ReadContainerObject` binds. The interpreter copies a bound memory into
    the register, which the kernel would alias instead.
  - The kernel gives each such register its own elements-storage container,
    initialized to the type's default.
  - `ContainerWrite` compiles to a blocking `mem_write`, and the reference path
    handles it.
  - Frames that list private arrays save and restore their contents, keeping
    preserved results, exactly as the interpreter's frames do. Such a member runs every
    frame operation through the generic bridge, compiled code included.
  - The 64-container-register compile limit is removed; nothing depended on it.
  - The codec testbench threads now run in the kernel. The old JIT's 7 s wait is gone,
    and original_codec simulate went from 16.8 s to 9.95 s, of which kernel code
    generation is now 3.0 s.
- **Parity.**
  - Nine cases print exactly what the frozen build printed (sorted lines).
  - mixed_codec prints 11 fewer `numeric to_integer detected a metavalue` warnings
    (decoder lines 263 and 265 before reset). The default engine without the kernel
    prints the new counts (24 and 9), so the frozen v4 build's extra warnings came from
    running the testbench on the host.
  - Vivado prints none of these warnings.
- **Element-net families read and written per leaf.**
  - The codec RTL's unpacked arrays (Chien–Forney `spe`, `se`, `sigma_next`; syndrome
    `s_mult`; encoder `fbm`; 32–33 leaves of 8 bits) are kernel families. Every whole
    read assembled a 264-bit `PackedLogic4`, and every element write rewrote the family:
    60% of original_codec's run.
  - A family read whose register feeds only constant extracts, each inside one leaf,
    now loads those leaves in place (`field_family`). This applies when the member
    writes neither the family nor its leaves and no barrier lies between the read and
    its last selection.
  - A SystemVerilog slice write that covers exactly one leaf stores that leaf.
  - original_codec's kernel run went from 5.2 s to 2.95 s, with output unchanged.
- **Kernel-held memories in native code.**
  - Elements-storage containers (behavioral members' private arrays) get
    `StaticKernelContainerInfo` storage 3.
  - Native code reads them, and writes them in place when blocking; nothing observes
    them.
  - Frames save private arrays per thread (the interpreter gives each fork branch its
    own frame stack) and only for callables whose frames list one.
- **Signal-backed memories written inline.**
  - Blocking element writes of element-slot memories (storage 1) and packed memories
    (storage 2) are now native.
  - Storage 1: the element table carries each slot's silence. Storage 2: the info
    carries the packed slot's silence (`packed_silent`). Writes follow the slot-store
    sequence: written when changed or silent, readers notified, the packed case with
    the field's offset.
  - The throughput testbench's 578 k helper calls went away, with no measurable time
    change: they were cheap.
- **Two-state code for SystemVerilog (`FSIM_STATIC_KERNEL_TWO_STATE=2`, opt-in).**
  - Enabling it for SystemVerilog gave wrong results, found by bisecting templates. Local
    registers live in allocas, but the handover continues full code from the register
    file. VHDL never hit this because its two-state templates always have a
    `shadow_base`.
  - Fixed: no local registers in two-state code, nor in full code with continuations.
  - It stays off by default. SystemVerilog members commonly hold X until reset, so most
    templates also need their full code. On the throughput case, code generation went
    from 250 ms to 540 ms and simulate from 2.53 s to 2.96 s.
- **Constant evaluation, continued.**
  - Specialization actuals are parsed lazily in the semantic `HirIntegralEvaluator`.
    Each construction used to parse every parameter.
  - Widths of declaration and callable types are cached per specialization
    (`record_type_widths`).
  - Constant functions whose bodies use `if`, loops or `case` over call-free expressions
    that only assign locals may now fold. An example is `alpha_pow_fn(PRIM_POW*apg)` in
    a 256-way generate. Before, 3456 callable bodies were lowered per elaboration, about
    0.57 s.
  - The evaluator gained a word-level path for Logic4 operands up to 64 bits, matching
    the general path's semantics (operand extension, wrap, signed division). The
    general path handles the rest, including errors.
  - The recursion guard is a small stack, not a hash set.
  - original_codec elaboration went from 6.4 s to about 5.0 s.
- **Process canonicalization.**
  - Representatives already passed the shareability check, so `share_operations` no
    longer rescans both lists (`shareable_checked`).
  - A template instance tries the template's last representative before building its
    grouping key.
  - Canonicalization went from 13.6% of codec elaboration to 3%.
- **Behavioral templates compile lazily, by work.**
  - Testbench threads are large and mostly cold: the codec testbench has six templates
    of about 11 k instructions, and almost none of the kernel's work.
  - Their templates are now lazy. The KIR interpreter runs them until their interpreted
    instructions reach size × (200 + size / 10), roughly the compile cost, which keeps
    the choice within about twice the better one (`note_lazy_work`).
    `FSIM_STATIC_KERNEL_LAZY_BEHAVIORAL=0` compiles them at once.
  - original_codec simulate went from 6.4 s to 5.0 s. The throughput testbench template
    (2 k instructions, 11 M instructions of work) is still compiled early, so the
    throughput cases are neutral.
- **Reference `binary_value` on words.**
  - Logic4 operands of at most 64 bits use `kernel_word::binary`, as kernel-compiled
    code does for every operator except `vhdl_match_equal`.
  - The VHDL time-0 prologue runs on the reference path. On mixed_codec the time before
    the interpreter starts went from 471 ms to 205 ms, and the run from 4.0 s to 3.6 s.
- **VHDL initializer memo across generate occurrences.**
  - Memo entries record whether their evaluation read a hierarchy identity (generate
    parameters, named hierarchy identities). Entries that did not serve overlays that
    differ only in hierarchy identities, with the same selected generates.
  - The trigger: `GEN := gen_poly(...)` in `rs_encoder_top.vhd`, sliced per tap in a
    for-generate, was evaluated 145 times (787 ms); now it is evaluated 8 times.
  - mixed_codec elaboration went from 4.9 s to 4.15 s.
- **Lazy codegen threshold checked.** Interpreting lazy templates instead of compiling
  them costs far more: throughput simulate 2.5 → 3.6 s, mixed 2.7 → 5.0 s.
- **overlay_class** indexes classes by unit; the linear scan was quadratic in the class
  count.
- **Diagnostics.**
  - `FSIM_PROFILE_KERNEL_PLAN` now names the rejected operation and lists every
    unsupported kind.
  - New replay tool `r37-analysis/tools/replay.py`: it reruns a campaign sample's
    recorded commands with another binary.

### 2026-10-07: Verilator reference (D6 second clause)

Verilator 5.052 (conda-forge, installed under `~/.local/opt/verilator`) on the Verilog
throughput round-2 fixture:
- **Build.** `--binary --timing -O3 --x-assign fast --x-initial fast`, C++ at
  `-O2 -march=native`, single-threaded. The build takes 25 s.
- **Parity.** Its `THRU` and `STIM_SUMMARY` lines match the Vivado transcript exactly.
- **Speed.** It runs in 0.30 s on CPU 9: 4.09 G instructions, 1.49 G cycles.

fsim's simulate phase on the same fixture (HEAD `d0c96e87`):

| Part | Time |
|---|---|
| Whole phase | 2.65–2.73 s: 35.2 G instructions, 12.1 G cycles |
| Process start | 0.08 s |
| Design load | 0.38 s |
| Kernel build | 0.53 s, of which code generation 0.26 s |
| Run | 1.67 s |

- **Ratios.** 8.8× Verilator for the phase, 5.5× for the run alone. The clause needs
  at most 2×, which is about 0.6 s.
- **Run-phase profile.**
  - Generated code: 45%.
  - Kernel runtime: 37% (notify, slot changes, slot writes, scheduling).
  - Lazy cold code generation: 15%.
  - Counts: 10.8 M native runs, 17.6 M notifications, 28.2 M schedules and 3.64 G
    SimIR operations over 44.7 k clock cycles.
- **Two-state code for Verilog templates fails.** Enabling the VHDL two-state path for
  Verilog templates gave wrong results: instance 2 reported `errs=22`. It was also
  slower: 39.9 G instructions. Reverted.
- **What closing the gap needs.** The plan's static scheduling of synthesizable logic
  (P2), ahead-of-time code generation at elaborate (P1), zero-copy artifact load, and a
  known-value fast path that is exact for Verilog (P5).
- **Commercial evidence.** Public evidence that commercial simulators reach 2× of
  Verilator is thin: Verilator's documentation claims "similar or better performance"
  than VCS and Xcelium, and the OpenHW CVW project reported Verilator more than twice
  as fast as VCS. Licence terms forbid published vendor benchmarks.

### 2026-10-07: D6 qualification (`v4-d6b-pair-1007`)

Frozen candidate `.local-artifacts/v4cand-d6b-1007` (the state of this commit). The full
suite fails only the control's 37 tests, and both kernel transcripts are identical to the
reference engine's.

| Case | Compile | Elaborate | Setup + simulation | Total | HEAD control | xsim |
|---|---:|---:|---:|---:|---:|---:|
| Verilog | 0.29 | 1.95 | 2.65 | **4.94** | 31.84 | 18.63 |
| Mixed | 0.54 | 1.49 | 2.75 | **4.82** | 26.06 | 15.10 |

Preflight, the absolute targets and the anti-slowdown guard passed. Against the control,
the paired ratios are 0.155 (Verilog) and 0.186 (mixed); `v4-d6a` gave 0.151 and 0.194.
The lazy codegen threshold costs Verilog a little and gains mixed more.

### 2026-10-07: paired D6 measurement, kernel dispatch and commit trims

**Paired campaign `v4-d6a-pair-1007`** (7 pairs, CPU 9, frozen candidate
`.local-artifacts/v4cand-d6a-1007`: mimalloc, two-state VHDL code, the fast dispatch
below). Preflight and the anti-slowdown guard passed.

| Case | Compile | Elaborate | Setup + simulation | Total | HEAD control |
|---|---:|---:|---:|---:|---:|
| Verilog | 0.29 | 1.90 | 2.40 | **4.58** | 30.36 |
| Mixed | 0.53 | 1.44 | 2.75 | **4.72** | 24.33 |

Verilog meets D6 (about 5.6 s). Mixed was 0.12 s over its 4.6 s, so these followed:
- **Fast dispatch.**
  - An ordinary member run touched the roughly 600-byte member record first, and that
    load was a cache miss (47% of `run`).
  - A compact `FastRun` record per member now holds the entry, bindings, program and
    body. It is refreshed after every other run, and anything unusual takes the old
    path.
  - Mixed simulate cycles fell 5.7% (14.20G to 13.39G). The rounds' vectors also keep
    their capacity (no allocation per round).
- **Commit records.** `commit_round` reads a 64-byte `CommitSlot` per slot (arena
  location, before-image, touched flag) instead of the slot record, and skips the
  `last_writer` store for single-writer slots. Cycles fell 2.3%.
- **Round order.** Rounds of more than 32 members are ordered through a member bitmap
  instead of a comparison sort. Cycles fell 1.9%.
- **Lazy codegen threshold** went from 256 to 1024 runs. Mixed simulate needs 2.3% fewer
  cycles, Verilog 1.2% more; Verilog has the margin.
- **Measured but not kept.**
  - Continuing two-state handovers in the KIR interpreter instead of compiling full code
    on the first one halved the single-template compiles (95 to 42) but cost 1.6% more
    cycles.
  - Zeroing 'U' literals that inserts overwrite completely made more templates two-state,
    but they then handed over more often: 0.8% more instructions.

### 2026-10-07: two-state VHDL code, simplifying IR builder

`e2e.py`, interleaved with the frozen build `.local-artifacts/v4ref-mimalloc-1007`
(mimalloc, before these two changes):

| Case | Build | Compile | Elaborate | Simulate | Total |
|---|---|---:|---:|---:|---:|
| Mixed | reference | 0.49 | 1.40 | 3.33 | 5.21 |
| Mixed | current | 0.50 | 1.41 | 3.16 | **5.07** |
| Verilog | reference | 0.40 | 1.92 | 2.63 | 4.95 |
| Verilog | current | 0.41 | 1.90 | 2.60 | **4.91** |

Parity holds on both designs: transcripts are identical, and mixed matches earlier builds
byte for byte. `FSIM_KERNEL_VERIFY` found no mismatch in five minutes of the mixed run.
- **Simplifying IR builder.** The emitter builds with `InstSimplifyFolder`, so `x | 0`,
  `x & ~0` and constant branches fold as they are created; the cold tier runs no clean-up
  passes. Eager cold codegen went from about 0.48 s to 0.45 s, with mixed simulate about
  0.04 s faster.
- **Two-state VHDL code.**
  - An upper-bound experiment that drops the X plane and 'U' mask in VHDL templates
    (unsound, but its transcript was identical) took mixed simulate from 3.27 s to 2.77 s.
  - In the real version, every VHDL template, except those with X- or U-carrying
    literals, compiles to two-state code that assumes every register's X plane and 'U'
    mask are zero.
  - An instruction whose result has X or 'U' bits (a load, a helper result, a generic
    operation) stores them and hands over to the template's full-semantics code at the
    next instruction (frame status 4, the instruction in `reserved`).
  - The full code is compiled on first need, with those instructions as entries. Both
    versions come from the same KIR, so the state is consistent.
  - A member runs two-state code only while its registers are known. The kernel rechecks
    after other runs and clears the planes of registers that are not live at entry.
  - A member that keeps meeting X or 'U' values switches to the full code: more than 15
    handovers covering at least an eighth of its two-state runs, or 64 runs with unknown
    registers.
  - Mixed simulate went from 3.26 s to 3.04 s (`FSIM_STATIC_KERNEL_TWO_STATE=0` versus on).
- **What did not work.** Resuming the reference evaluator after a guard (a SimIR
  operation) gave wrong values. The specializer fuses a wide `ReadSignal` into a KIR load,
  so the SimIR register it fills never exists in compiled state. Continuing in compiled
  full code at the KIR level avoids that.

### 2026-10-07: mimalloc, more exact VHDL templates, lazy cold codegen

Measured with `e2e.py` (medians of 3), parity identical on both designs, and the mixed
transcripts byte-identical to earlier builds.

| Case | Compile | Elaborate | Simulate | Total |
|---|---:|---:|---:|---:|
| Verilog | 0.40 | 1.84 | 2.47 | **4.71** |
| Mixed | 0.49 | 1.43 | 3.40 | **5.32** |

- **Allocator.** The owner asked about mimalloc and chose to vendor it.
  - Source: `third_party/mimalloc-3.5.3`, the unmodified upstream release archive with a
    manifest, SBOM and supply-chain rows. `cmake/FsimMimalloc.cmake` verifies and
    extracts it.
  - The `fsim` executable links mimalloc's `static.c` as its C and C++ allocator.
    `FSIM_MIMALLOC` controls it, and it is off for sanitizer and allocation-profiling
    builds.
  - On Windows a DLL with the upstream redirection module does the same. That path has
    not been built or run on a Windows host.
  - The glibc `hugetlb` re-exec in `main.cpp` is removed.
  - Preload A/B in fresh workspaces:

    | Allocator | Verilog | Mixed |
    |---|---:|---:|
    | glibc with `hugetlb` | 5.49 s | 5.69 s |
    | mimalloc 2.1.7 with large OS pages | 4.85 s | 5.45 s |
    | mimalloc 3.5.3 | 4.77 s | 5.18 s |

  - mimalloc 3.5.3 gains the same with or without large OS pages, so Windows needs no
    special privilege.
  - The extra glibc tunables (`mmap_threshold`, `trim_threshold`, `top_pad`) gained only
    about 0.03 s.
- **Exact VHDL templates, second step.**
  - Scalar entries keep their original restrictions. Any occurrence they exclude
    (variables, calls, partial drivers, unit processes in architectures with if-generates)
    is stored as an exact entry.
  - The remap now maps clock and gate reads, signal attribute queries (`'event`,
    `'last_value` and the like), projected and dynamic slice writes, and `Assert`.
  - VHDL process lowering went from 0.20 s to 0.11 s of CPU (61 of 170 replay), and mixed
    elaboration from 1.76 s to 1.64 s before mimalloc.
- **Lazy cold codegen.**
  - Cold member templates are compiled once they have run `FSIM_STATIC_KERNEL_LAZY_RUNS`
    times (default 256; 0 compiles everything at once), in batches of 32. Until then they
    run on the KIR interpreter, which is about 3× slower than native code.
  - Mixed codegen went from 0.53 s to 0.28 s (286 templates in 11 batches) and simulate
    by about 0.07 s; Verilog is unchanged.
  - Thresholds: 64 gave no gain, 256 and 1000 gained about 0.07 s, 4000 lost.
- **Measured but not changed.**
  - Static warm and hot tiers for more member templates: no gain on mixed at hot
    thresholds of 8192 or 2048.
  - Word loops instead of `memcpy`/`memcmp` in `commit_round`: neutral, because the cost
    is memory access to the slots.
  - Element port actuals (`a => S(gi)`) are copied by processes that add a delta the LRM
    does not have (about 2.4M mixed runs). Aliasing them would change elaboration
    semantics, so it is not done.

### 2026-10-07: D6 work after Phase 1 (compile, elaboration, runtime state)

End-to-end harness `r37-analysis/tools/e2e.py` (fixture roots, fresh workspaces, CPU 9,
static kernel, medians of 3). The frozen Phase 1 binary measured 6.48 s (Verilog) and
7.88 s (mixed) in the same harness.

| Case | Compile | Elaborate | Simulate | Total |
|---|---:|---:|---:|---:|
| Verilog | 0.47 | 2.10 | 2.66 | **5.23** |
| Mixed | 0.60 | 1.76 | 3.51 | **5.87** |

Both kernel transcripts match the reference engine (`kernel_parity.py`), and the mixed
transcripts are byte-identical to those of the build before the template changes. The
full suite fails only the control's 37 tests.
- **Compile.**
  - Per-object source mappings no longer re-normalize every source span's file name.
  - The workspace checks the design's structure once for all its objects (new
    `extract_compiled_object_set`).
  - `normalize_compiled_design` indexes HIR records by ID instead of searching linearly.
  - Span interning remembers canonicalized source paths while one model is built. The
    fixture roots reach the sources through a symbolic link, so each span cost several
    `readlink` calls.
  - Fixture VHDL compile went from 0.88 s to 0.50 s, Verilog compile from 0.84 s to 0.47 s.
    The compiled HIR objects are byte-identical.
- **VHDL validation memo.** Type and declaration validation is a pure function of the
  unit, its entity and the specialization actuals. Combinations that validated without a
  diagnostic are skipped when another instance repeats them. Mixed elaboration went from
  2.88 s to 2.33 s.
- **Exact VHDL templates.**
  - An occurrence whose specialization overlay equals a cached one's replays the cached
    process, including generated concurrent statements, any statement kind, processes
    with calls and local variables, and the unit's own signals.
  - Signal layouts now compare array element types structurally.
  - Templates are indexed by unit, statement, process and generate position.
  - Mixed elaboration went from 2.16 s to 1.76 s. 2,245 of 2,989 exact occurrences replay.
- **Constant-driver merge.** Each instance's constant generated drivers become one process
  (`FSIM_MERGE_CONSTANT_DRIVERS=0` keeps one each). Verilog went from 2.43 s to 2.35 s
  elaborate and from 2.87 s to 2.70 s simulate. The reference engine loses its compact
  startup writes; the owner accepted that, since it will be replaced. Elaboration tests
  that check per-statement shapes run with the merge off, and a new test covers the
  merged form.
- **Runtime state schema 76.**
  - Shared operation bodies are written verbatim.
  - Instances store override indices and debug-scope remaps.
  - Plain strings use a table.
  - With an external path table, any string-table entry that spells a hierarchy path is
    written as its path ID, so payloads carry no paths.
- **Profiling switches.** `FSIM_PROFILE_LOWERING=0` keeps the lowering census out of phase
  profiles; the census otherwise still follows `FSIM_PROFILE_PHASES`.
- **Remaining mixed costs** (simulate 3.5 s):
  - setup: decode 0.13 s, KIR compile 0.18 s, cold codegen 0.52 s;
  - run: about 2.65 s, with 10.8M combinational and 6.8M sequential member runs;
  - `commit_round` with its small `memcpy`/`memcmp` is about 11%;
  - about 1.3M helper or generic calls (container writes from the testbench,
    `dynamic_part_select`, 256-bit generic inserts).

### 2026-10-07: local registers, heap huge pages, host and planner trims

Both transcripts stay identical after every change (`kernel_parity.py`), and the three
static-kernel test targets pass.
- **Local registers in native code.** A KIR register that every run writes before reading
  (a must-defined forward dataflow over basic blocks, loops included) carries nothing from
  one run to the next. In bodies that never resume from the register file (not VHDL, so no
  deoptimization; not behavioral; no `generic` instruction), such registers become stack
  slots that SROA promotes to SSA values, instead of being stored to the frame on every
  write. Templates with a VHDL instance keep the frame (`StaticKernelTemplate::vhdl`).
  - 7,356 template registers, 6,813 local. Verilog simulate 4.27 s to 3.97 s; the hot O2
    templates compile in 265 ms instead of 325 ms. `FSIM_STATIC_KERNEL_LOCAL_REGISTERS=0`
    restores the frame stores.
- **Heap huge pages.** A large design's heap is hundreds of megabytes of small
  allocations: Verilog simulate took 214k minor page faults and 0.38 s of system time.
  With transparent huge pages in `madvise` mode, glibc asks for huge pages only when its
  `glibc.malloc.hugetlb` tunable is set, and it reads tunables only at process start. The
  `fsim` executable now re-executes itself once with the tunable set (`execv` of
  `/proc/self/exe`: same process, arguments and descriptors). Library and test callers are
  unaffected, and `FSIM_HEAP_HUGE_PAGES=0` keeps the default heap.
  - Faults fell to about 13k, system time to 0.08 s, and user time fell too (fewer TLB
    misses). Verilog simulate 3.85 s to 3.2 s; mixed simulate 4.0 s to 3.6 s; both
    elaborations by about 0.5 s.
- **Host start for a closed kernel.** Inert kernel members (no sensitivity, never
  initialized or final on the host) are skipped by the static fanout, start queueing,
  final-process scan, native dependency masks, SystemVerilog update-pool sizing and the
  switch scan of owned-driver composites. Kernel-owned signals get no host composite (the
  host never resolves them; a missing composite only selects the general driver path).
  `fusion_dormant_process` now marks the kernel host 2 so these skips can tell it apart.
  The region snapshot's member stubs share one operation body, and its builder no longer
  formats per-process purity diagnostics unless they are being profiled.
  - Start: about 265 ms to 45 ms.
- **Planner.** Edge-sensitive signals are computed once per ownership pass instead of
  scanning all candidates for each alias-family proxy: alias and fixpoint 55 ms to 9 ms.
- **Load.**
  - The restored design resolves its signal, string and container names directly in the
    artifact's canonical path table (`CanonicalHierarchyPaths`) instead of building its
    own table and remapping onto the canonical one. Under deferred validation (simulate)
    only those names are resolved, since elaboration froze exactly this design's paths
    into that table; other loaders still check every path. Freeze: about 150 ms to
    20 ms.
  - Restored interned paths are interned once per path ID.
- **Kernel setup.** Writer classification uses a dense VHDL flag per process and skips
  single-writer slots (43 ms to 2 ms). Partition construction uses one member-indexed
  scratch vector instead of per-group hash maps. Per-member `getenv` calls are hoisted.
- **Hot templates use fast instruction selection.** The optimizing backend's register
  allocation and machine passes carry most of its benefit. Fast selection halves the
  hot templates' compile time (about 265 ms to 115 ms) for about 100 ms more run time
  on this design. `FSIM_STATIC_KERNEL_HOT_FAST_ISEL=0` selects the full selector.
- **Notifications.** `native_notify` sits beside `slot_changed`, whose notify-target loop
  is inline. The ranged-reader, output, waiter and edge work is in
  `slot_changed_general`.
- **Measurements.** Wall times on this VM drift by 5–10%, so changes were compared
  against a frozen reference build, run interleaved (`.local-artifacts/v4ref-10070935`).
  - Verilog simulate: 4.27 s to about 3.07 s (`rep.py`).
  - Verilog elaborate: 3.43 s to 3.03 s.
  - Mixed: elaborate 2.93 s, simulate 3.62 s.

### 2026-10-07: startup trims (load validation, run-once members, shared analyses)

Each change below leaves both transcripts identical, and the elaborated snapshots
byte-identical where elaboration is touched.
- **Simulate skips the loader's trial population.** `from_state` populated a throwaway
  interpreter with every process program only to validate it. The simulate command then
  populates the real interpreter, which validates the same programs through
  `add_process_program_impl`, so `handle_simulate` now loads under
  `elaboration::detail::DeferredProgramValidation`. Other loaders, including the codec
  tests, still validate at load, and payload checksums are still verified.
  - Verilog simulate fell by about 0.3 s: the 151 ms population plus destroying the
    trial interpreter.
- **Run-once members are not compiled.** About 35k Verilog members are constant drivers
  that run once at start on the reference evaluator. Templates fell from 230 to 209.
  `FSIM_STATIC_KERNEL_COMPILE_ONCE=1` compiles them again.
- **Region graph: static read ranges once per operation body.** Processes that share a
  body have equal read widths, so the analysis runs 1,478 times instead of 85,547.
  - Load build: ranges 48 ms to about 1 ms. Elaboration: 88 ms to about 2 ms.
- **`freeze_hierarchy_paths`** reads debug-point scopes through `debug_point()` and
  `debug_scope()` instead of copying all 175k operations. Runtime decode fell from 603 ms
  to 560 ms.
- **`plan_static_kernel`** checks stored operations instead of expanded copies, mapping
  signal operands through `OperationList::signal()`. Plan: 144 ms to 110 ms, with the same
  members, owned signals and containers.
- **Now** (`rep.py`, CPU 9):
  - Verilog: elaborate 3.43 s, simulate 4.27 s.
  - Mixed: elaborate 2.99 s, simulate 3.94 s.

### 2026-10-07: kernel-owned time

- **What changed.** A closed kernel can run its own next time steps. Closed means every
  process is a member, there are no outputs and no host-written inputs. It does so when
  its next event is its own timer and the host scheduler has nothing pending up to that
  time (`Scheduler::next_pending_time`).
  - Both throughput cases fell from 134k kernel activations to 33 (134,048 warped steps).
- **Exactness.**
  - `kernel_now()` supplies the warped time for `%t`, the output and report hooks, and
    `WaitFor`. Output in a warped step reports delta 0, where the scheduler would have
    run it.
  - `$finish` in a warped step schedules a host task at that time. The scheduler reaches
    the same tick and delta before stopping, so the stop line is unchanged.
  - Kernel members never read `$time` directly; `ReadSimulationTime` is not admitted.
  - Both transcripts are byte-identical, the full test suite shows the 37 control
    failures and no others, and the static-kernel tests pass.
- **Gating.** `Simulation::run` allows the warp only when nothing outside the kernel
  needs each step. It is refused under any of:
  - a run limit;
  - active PSL directives (`VhdlPslExecution::active`);
  - signal or scalar observers or change hooks;
  - safe-point observers;
  - VPI handles or runtime updates;
  - SystemC.

  The kernel also checks the interpreter's change hooks, and gives the host one
  activation every 4,096 steps (interrupt latency). `FSIM_STATIC_KERNEL_TIME_WARP=0`
  disables it.
- **Simulate** (`rep.py`, five runs, CPU 9): Verilog 4.80 s to **4.68 s**, mixed 4.56 s to
  **4.18 s**. The warp alone measured 4.80 to 4.60 s and 4.56 to 4.18 s.

### 2026-10-07: paired measurement after the mixed kernel and specialization

Paired run, 7 pairs, CPU 9, against HEAD e8ecb67e, with the candidate frozen as
`v4cand-mixspec2-10070723`. Evidence:
`.local-artifacts/simulation-performance/v4-mixspec-pair-10070723`. Status
`qualified_absolute_targets`: preflight, parity on every pair and the anti-slowdown guard
all pass.

| Case | Control | Candidate | Compile | Elaborate | Simulate |
|---|---:|---:|---:|---:|---:|
| `original_throughput` | 31.68 s | **8.80 s** | 0.59 | 3.45 | 4.76 |
| `mixed_throughput` | 25.59 s | **8.40 s** | 1.14 | 3.05 | 4.21 |

- Verilog/mixed is 1.048, inside the contract's 1.05. Mixed improved more than Verilog in
  this round, so Verilog work is next to keep the margin.
- Against the previous paired run, Verilog went from 9.72 s to 8.80 s and mixed from
  13.47 s to 8.40 s.
- **Phase 1:** total ≤10 s is met. Simulation ≤3 s is not (4.76 s).
- **Full test suite:** the 37 control failures and no others, after updating
  `runtime_direct_artifact_rows_test`'s list of unsupported schemas to 73 and 75.

### 2026-10-07: mixed-language kernel by default, VHDL body specialization

**One kernel for both languages** (owner direction: VHDL and Verilog in one kernel; design
in §4c).
- A design with VHDL and SystemVerilog candidates now plans one mixed kernel by default
  (`FSIM_STATIC_KERNEL_MIXED=0` keeps the majority language only). If the mixed plan is
  refused, the planner retries without it.
- **Fix: first-round initialization.** VHDL `once` members (constant port actuals such as
  `b => to_unsigned(i+2, 4)`) ran before the first round and their deferred writes were
  lost. They now join the first VHDL round, which also fixes the `static_kernel_vhdl` test
  in mixed mode.
- **`vhdl_unowned` only for VHDL accesses.** A host-routed signal whose kernel readers
  and writers are all SystemVerilog members is exchanged through the host, as in a
  SystemVerilog kernel. The `rs-vhdl` `rs_thru_tb` (raw `$urandom` testbench: one
  testbench process stays on the host and shares `cyc` with a kernel thread) had
  disabled the whole kernel.
- `mixed_throughput`: all 5,036 members compiled (the SystemVerilog testbench as
  behavioral threads), no host outputs, transcript byte-identical to the reference
  (17,032 lines, 17,018 HDL reports). Simulate 8.93 s to 6.99 s (`rep.py`, CPU 9).

**VHDL body specialization** (`src/runtime/simir_static_kernel_specialize.cpp`).
- **Problem.** On mixed, one template took 16.5% of all samples: the VHDL `gf_mult`
  (540 instances). Its function computes a GF(2^8) product with nested `for` loops, index
  arithmetic and range checks, about 2,500 KIR operations per evaluation. The Verilog
  version unrolls the same structure at elaboration with `generate`.
- **What it does.** At kernel construction, for each VHDL member whose activation has a
  loop, a partial evaluator walks the body with the reference evaluator (`step_generic`)
  on the registers whose values are known:
  - calls inline (the fixed-register call stack becomes static);
  - loops with constant control unroll;
  - range checks, index arithmetic and constant subexpressions fold;
  - a strict dynamic select or insert with a known index becomes the static one;
  - branches on signal values stay. Paths merge again where their known values agree.
- **Why it is exact.**
  - The output is ordinary SimIR, appended to the member's operations; activations enter
    it, and the first run still takes the original body through the prologue.
  - The reference evaluator, deoptimization (which resumes at a SimIR operation), 'U'
    tracking and `FSIM_KERNEL_VERIFY` all treat it like any other body.
  - Known values come from the reference evaluator itself. Every effect (signal reads
    and writes, assertions and reports) is emitted unchanged and in order, after its
    operands are materialized.
  - Diagnostics map the new operations to the originals (`Member::origin`).
- **Limits.**
  - A body is kept only if it is at most 2× (+64) its reachable size and it repeats no
    read of a signal wider than 64 bits; such reads must stay single-definition so the
    compiled tier can load just the selected field.
  - Bodies with operations outside the understood set, recursion, or non-isolated
    frames are left alone.
- **Cost.** Results are cached per shared canonical operation list. Lists whose only
  instance overrides are signals, report texts or debug points share one result.
  - 4,651 members are specialized, 3,348 of them from the cache, in 91 ms.
  - Codegen is unchanged (0.71 s; KIR 37k to 57k instructions).
- **Result on `mixed_throughput`:**
  - The interpreter phase fell from 5.10 s to 3.04 s (`FSIM_PROFILE_PHASES`).
  - Simulate wall time fell from 6.99 s to **5.17 s** (`rep.py`, five runs, CPU 9).
  - Transcript and HDL reports are byte-identical, and a 5,000 ns `FSIM_KERNEL_VERIFY`
    run is clean.
  - `FSIM_STATIC_KERNEL_SPECIALIZE=0` disables it, and `_DEBUG=1` reports each member.
- **Redundant reads.** A VHDL signal does not change during an activation unless the
  member writes it. The specializer therefore drops a repeated plain read of a signal into
  a register that already holds it. This keeps unrolled loops over array signals
  single-definition, so their fields still load in place.
  - A cached recipe is reused only if the mapping from operation to signal is a bijection
    between the two instances.
  - A size cap of 3× instead of 2× specializes 18 more clocked processes but saves only
    about 0.08 s of run, against more codegen. The cap stays at 2×
    (`FSIM_STATIC_KERNEL_SPECIALIZE_FACTOR` is a diagnostic).

**Fewer helper calls from generated code.** On mixed, generated code called the reference
helpers 1.85M (`evaluate`) and 1.88M (`effect`) times, mostly through `PackedLogic4`
conversions (about 12% of run instructions).
- **Copy propagation in the specializer.** A register copy of an unknown register of the
  same kind becomes an alias: reads use the source, and the copy is emitted only before
  the source changes, or at the exit when the copy is live.
  - Liveness comes from `prune_operations` on the original body, so only live
    registers are materialized at the exit.
  - Where paths may meet, aliases are made real, and signal-holding facts match as a
    subset. Without this, paths that differed only in a deferred copy never merged: the
    GF multiplier grew 8 copies of its exit and its O2 compile cost about 0.3 s.
- **Static part selects.** A `DynamicPartSelect` with a known base is rewritten to an
  `Extract`. This applies when the selection is in range of a source register holding a
  signal's value (so the width is known), the case the reference evaluator itself takes
  through `extract_value`. Functions such as `get_slice(v, idx, w)` called on wide signals
  now read fields straight from the slot.
- **Indexed VHDL signal stores** (`store_vhdl` sub 1) append to the deferred-write queue
  in generated code. An unknown or out-of-range index still takes the reference path,
  which reports the error. `imm_a` carries the target width.
- **`load_field9` decodes X, Z and 'U' inline** (`kernel_word::logic9_uword`; 'U' only
  into a tracked register). The coerced variant is total. W, L, H and '-' still take
  the reference path.
- **Result.** Helper calls fell to 334k `evaluate` and 595k `effect`. KIR fell from
  57k to 50k instructions.

**Three codegen tiers.** After these changes the one hot template, the specialized GF
multiplier, cost more to compile at O2 than it saved: 540 members that each run rarely.
Templates now have three tiers:
- **cold:** FastISel at O0;
- **warm:** `sroa,early-cse,simplifycfg` with SelectionDAG at O0, for heavy member
  templates;
- **hot:** O2, for heavy partition templates only. Partitions rerun whole on every input
  change.

| Hot backend for every heavy template | Mixed simulate | Verilog simulate |
|---|---:|---:|
| O2 | 4.96 s | 5.04 s |
| O0 SelectionDAG | 4.67 s | 5.31 s |
| Split by tier (adopted) | 4.72 s | 5.02 s |

**Current** (fresh workspaces, `kernel_parity.py`, both transcripts identical):
- mixed simulate 4.58 s;
- Verilog simulate 4.80 s.

**Runtime artifact string table** (runtime state schema 73 to 74).
- **Composition.** On Verilog, the 69 MB `runtime.bin` is mostly per-instance operation
  overrides: 40 MB across 85,547 process rows.
  - 26.6 MB of that is 175,918 `DebugPoint` overrides, each carrying its source path as a
    full string. Shared templates span different generated statements, so their debug
    points differ per instance.
- **Change.** Runtime state now writes each distinct `InternedString` once; later
  occurrences are an index. The decoder rebuilds the table, so repeated strings are no
  longer parsed or interned again.
- **Effect.**
  - `runtime.bin`: Verilog 69.4 MB to 48.3 MB; mixed 16.0 MB.
  - Simulate: −0.33G instructions, but cycles are within noise. Decode cost is dominated
    by object construction, not bytes.
- **Tests updated.** Three tests pin the runtime schema at 74:
  `application_test_artifact_phases`, `schema_identity_test` and
  `runtime_direct_artifact_rows_test`. The wire-format test helper
  (`runtime_direct_artifact_rows_test_support.hpp`) enables the table like the real
  codec.
- **Measured and not kept:**
  - Skipping the load-time validator populate saves about 0.96G instructions but no
    measurable wall time, and it weakens artifact validation.
  - glibc malloc tunables (`top_pad`, `trim`/`mmap` thresholds, `tcache`) give at most
    about 0.1 s on simulate and nothing on elaboration.

**Run-loop trims on mixed** (same transcript after each):
- **Unwritten inputs** (`StaticKernelRuntimeSpec::unwritten_inputs`). The mixed fixture's
  408 kernel inputs are constant port actuals that no process writes. They made every
  VHDL round yield to the host.
  - The per-round host boundary now counts only inputs some process writes, plus outputs.
  - Without one, the kernel yields every 256 rounds, so the host's delta limit still
    stops a design that never settles.
  - Unwritten inputs are scanned once per activation instead of after every round.
  - Activations: 356k to 134k. Simulate: 5.17 s to 4.89 s.
- **First run.** A VHDL member's first run executes the prologue on the reference
  evaluator up to the original body start, syncs registers, and continues in compiled
  code. Before, it ran the whole unspecialized body generically. Simulate 4.89 s to
  4.82 s.
- **Commits.** Single-word slot stores use a masked store instead of `copy_bits`, and the
  round's before-images are copied with `memcpy`.
- `vhdl_unowned` and the planner fallback are described above. `rs_thru_tb` of `rs-vhdl`
  (raw `$urandom` testbench) now runs as a mixed kernel: 8.3 s against 21.8 s on the
  reference engine.

**Validation of the frozen candidate** (`.local-artifacts/v4cand-mixspec-10070609`):
- **Vivado preflight:** all 10 cases pass stimulus parity
  (`.local-artifacts/simulation-performance/mixspec-preflight-10070609`).
- **Kernel tests:** the three `static-kernel*` tests pass. A 5,000 ns `FSIM_KERNEL_VERIFY`
  run on mixed is clean.
- **Verilog:** `original_throughput` transcript identical. Elaborate 3.67 s, simulate
  4.94 s (`rep.py`, CPU 9).

### 2026-10-07: paired measurement after the behavioral tier

Paired run, 7 pairs, CPU 9, against HEAD e8ecb67e, with the candidate frozen as
`v4cand-beh-*`. Evidence: `.local-artifacts/simulation-performance/v4-behavioral-pair-10070444`.

| Case | Control | Candidate | Compile | Elaborate | Simulate |
|---|---:|---:|---:|---:|---:|
| `original_throughput` | 34.66 s | **9.72 s** | 0.65 | 3.81 | 5.26 |
| `mixed_throughput` | 27.22 s | **13.47 s** | 1.24 | 3.26 | 8.93 |

- The Vivado preflight passed. The absolute verdict and the anti-slowdown guard pass.
- The machine was slower in this run: the control measured 27.22 s for mixed, against
  25.25 s at Phase 2.
- **Phase 1 status:**
  - total ≤10 s: met for Verilog (9.72 s);
  - simulation ≤3 s: not met (5.26 s).
  - Startup is now the larger part of simulation: about 3.3 s, against a 2.2 s run.

### 2026-10-07: behavioral tier on by default, shared templates, region-graph stubs

- **Behavioral tier on by default.** With `FSIM_STATIC_KERNEL=1`, behavioral members are
  admitted unless `FSIM_STATIC_KERNEL_BEHAVIORAL=0`.
- **Vivado preflight** (`perf_campaign.py --preflight-only --prepare-full`, kernel and
  behavioral tier on): all 10 cases pass stimulus parity. That is both throughput and
  codec cases (Verilog and mixed) plus the six codex cases.
  Evidence: `.local-artifacts/simulation-performance/behavioral-preflight-10070415c`.
- **Region-graph stubs.** The interpreter's runtime region snapshot gives dormant kernel
  members a one-operation stub (`WaitForever`, matching id and domain) instead of
  materializing their programs. Pre-run time fell from 0.46 s to 0.27 s. Graph consumers
  only lose fast paths for host signals that kernel members write.
- **Instance constants as bindings.** Units with the same shape (the template key
  without `KOp::constant` immediates; SystemVerilog only) share one template. The
  constants that differ between them are loaded from per-unit bindings (`sub` 1, aval
  and bval each as two 32-bit bindings).
  - Verilog: 401 to 307 templates, 91.6k to 51.7k instructions, codegen 1.11 s to
    0.69 s.
  - The 32 syndrome-cell templates still differ, by `load_field` bit offsets.
- **Field offsets as bindings.** Within a shape group, a `load_field` whose bit offset
  differs between units takes the offset from a per-unit binding (`sub` 1, `y`). The
  native code computes word and shift and selects the high word only when the field
  crosses a word inside the plane.
  - Verilog: 307 to 230 templates, 51.7k to 23.0k instructions, codegen 0.60 s.
  - The run phase fell from 3.65 s to 2.21 s; the shared code is far smaller.
- **Spec move.** `create_interpreter` moves the plan's specification into the kernel
  instead of copying it (85k members).
- **Phase breakdown of Verilog simulate (about 5.5 s):**

  | Step | Time |
  |---|---:|
  | Artifact load (runtime-state decode 0.89 s) | 1.18 s |
  | Kernel plan | 0.22 s |
  | Kernel codegen | 0.58 s |
  | Interpreter population and kernel construction | ~0.96 s |
  | Interpreter start | 0.34 s |
  | Run | 2.21 s |

  Simulation ≤3 s now depends on startup (plan item 2, the flat kernel image).
- **Simulate wall time** (`rep.py`, CPU 9, three runs each, two rounds):

  | Case | Before | Now |
  |---|---:|---:|
  | Verilog | 7.0 s (behavioral, before these changes) | 5.64 s and 5.81 s |
  | Mixed | ≈9.0 s | 8.51 s and 8.55 s |

  - Estimated Verilog total: 0.59 + 3.45 + 5.7 ≈ 9.75 s, which would meet the Phase 1
    total (≤10 s). Simulation (≤3 s) is not met.
  - In mixed, the SystemVerilog testbench stays on the host because the kernel runs in
    VHDL mode.

### 2026-10-07: behavioral tier stage 2 (compiled threads)

**What was built.**
- Behavioral members compile their whole operation stream to KIR:
  - waits, fork, `ForkEnd`, halt and `$finish` become `KOp::suspend`;
  - output, strings and plusargs run through the generic bridge (`behavioral_step`);
  - calls use the synthetic register stack;
  - isolated nonrecursive frames compile away.
- Native code enters through a switch over `CompiledBody::resume_entries`: operation 0,
  the operation after each suspension, and fork branches. A suspension returns 3 with the
  operation index in `StaticKernelNativeFrame::reserved`.
- The synthetic call stack is saved per thread while it is suspended, because fork
  siblings share the register file.
- A deoptimization moves the whole member to the reference evaluator (registers and
  every thread's stack).
- Suspension operations are barriers for in-place reads and wide-move fusion.

**Result on Verilog.**
- The six testbench `initial` blocks (2,098 operations each) compile; all 85,547
  members compile.
- All 12 result lines match.
- Testbench templates are shared across the six instances, so the 0.63 s host JIT
  barrier is gone; kernel codegen grows by about 0.14 s.
- Simulate wall time (`rep.py`, three runs, two rounds):

  | Configuration | Median |
  |---|---:|
  | Kernel | 9.04 s, 9.33 s |
  | Kernel + behavioral | 8.08 s, 8.06 s |

**Whole design in the kernel.**
- The region graph marks every write of a process that forks as `dynamic_fork_writers`,
  which made testbench-driven signals unownable. They stayed host signals, so the DUT
  read them through `load_host` (9.1M helper calls) and the testbench wrote them
  through `store_host` (2.3M).
- When every writer of such a signal is a behavioral member, its fork children are
  kernel threads, so the planner now owns the signal.
- The Verilog fixture then has 0 boundary inputs and 0 outputs. Helper calls fell to
  `evaluate` 0.44M and `effect` 0.86M; result lines match.
- A kernel with no boundary inputs parks its host stub on `WaitForever` (it runs at
  start and then by its own timers), because `WaitSensitivity` requires a sensitivity
  list.

**Kernel fix found by the wider Vivado preflight.**
- Symptom: `codex_reference_mode0_frames1` failed with "static kernel combinational
  logic did not settle", on the committed Phase 2 kernel too.
- Cause: an `always_comb` writes, several times per run, a module-level `integer` and
  accumulators that it also reads. Every intermediate write notified readers, so the
  running member, or its own partition, re-scheduled itself after each pass.
- In SystemVerilog a process is not waiting while it runs, so its own writes never
  wake it. The kernel now matches that:
  - a partition target whose earliest reader is the running member needs no requeue,
    because later readers run in the same pass;
  - a standalone combinational member ignores schedules while it runs.
- VHDL members notify only at round commit and are unaffected.

### 2026-10-07: behavioral tier stage 1 (interpreted threads)

**What was built.**
- Behavioral members are admitted behind `FSIM_STATIC_KERNEL_BEHAVIORAL=1`. The planner
  runs a second pass with `static_kernel_behavioral_operation_supported`, and host
  sensitivity is added for waited host signals.
- They run as kernel threads (`simir_static_kernel_behavioral.cpp`):
  - waits: delays through a kernel timer queue that arms one scheduler task for the
    earliest wake; `#0` through an Inactive list; `WaitOn` and `WaitSensitivity`
    through per-slot and per-input waiters with edge filtering;
  - fork/join (all, any, none) as threads that share the member's registers;
  - display, format, string and time display through the reference formatter and the
    host output hook;
  - string constants and copies, plusargs, automatic frames (packed, string and
    container registers), and `$finish` with the reference status capture.
- `activate()` drains the whole Active region (combinational settling, triggered
  members, ready threads) before `#0` resumptions and before the NBA commit.
- Kernel outputs are now published per slot: a value that changed before the
  activation's NBA commit goes to the Active region, otherwise to the NBA region.
  Before this change, a clock generated inside the kernel reached host testbench
  processes in the NBA region, and they sampled post-edge values one cycle late.

**Checks.**
- `tests/app/static_kernel_behavioral_test.cpp`: both engines, four behavioral members,
  identical transcript.
- Verilog round-2 fixture with the behavioral tier: all six testbench `initial` blocks,
  their fork children, monitors, clock, timeout and checker run in the kernel (85,547
  members, 0 host outputs). All 12 result lines match.

**Cost while interpreted.**
- The testbench's table and stimulus generation now takes 14.7 s before time 0, and the
  run takes 7.8 s against 5.0 s.
- The host JIT barrier is gone (1.5 ms against 0.68 s).
- Stage 2, compiling behavioral templates, is required before this pays off.

### 2026-10-07: option 1 (known-value fast path) tested and rejected

The plan prefers testing a thesis cheaply, so this measured the ceiling before building
anything.
- **Ceiling experiment.** A temporary, unsound flag treated every X/Z plane loaded from a
  slot or register as zero, and the IR was built with `InstSimplifyFolder`. Together these
  fold away all 4-state logic, which is the best a guarded known-value fast path could
  reach.
- **Result on Verilog.**

  | Measure | Normal | Assume known |
  |---|---:|---:|
  | Generated-code instructions | 2.13G | 1.61G (−25%) |
  | Total instructions | 9.08G | 8.24G (−9%) |
  | Codegen time | 0.96 s | 0.67 s |
  | Run phase | 4.88 s | 4.84 s |

  (Instruction figures are `perf record -c 1000000` event totals, so compare them only as
  ratios.)
- **Why.** Generated code takes 26% of cycles and 23% of instructions (IPC about 1.5).
  The instructions removed were executing in the shadow of the dependent loads: a binding
  load, then the arena slot, then the register file. The kernel run is bound by load
  latency, not by 4-state arithmetic. Whole-run counters: IPC 1.7, 1.3G L1 data misses,
  128M branch misses.
- **`InstSimplifyFolder` on its own** (sound) was neutral in wall time and added about 1.2G
  instructions, so it was not kept.
- **Conclusion.** A known-value fast path cannot deliver the ≤3 s simulation gate; at best
  it saves about 0.3 s of codegen. Reaching ≤3 s would need less work (activity, data
  layout, fewer dependent loads), not cheaper arithmetic. As decided, work continues with
  items 2–4 of §4a.
- **Diagnostics kept:**
  - `FSIM_STATIC_KERNEL_DUMP_IR=<dir>` writes each group module's IR after the pass
    pipeline.
  - `FSIM_PROFILE_PHASES` now also reports `static_kernel_plan` and
    `create_simulation_interpreter`.

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

## 4a. Phase 1 assessment (2026-10-06, after Phase 2)

Phase 1 is the plan's go/no-go gate: Verilog simulation ≤3 s and total ≤10 s, with the
rule "on a miss, stop and re-plan before investing in breadth". Current Verilog numbers
are from the Phase 2 paired run (compile 0.59, elaborate 3.45, simulate 8.77, total
12.82 s). The simulate phase measured with `FSIM_PROFILE_PHASES` on CPU 9 (9.24 s wall)
breaks down as follows.

| Step | Time | Notes |
|---|---:|---|
| Design artifact load | 1.04 s | runtime-state decode 0.79 s |
| Kernel plan | 0.14 s | |
| Kernel template codegen (LLVM O0, FastISel) | 0.88 s | 384 templates, 78k KIR instructions, 0.92M IR instructions |
| Interpreter population and kernel construction | ~0.77 s | all 85k processes still enter the host interpreter |
| Interpreter start and startup-tier JIT | ~0.58 s | includes a region graph over every process |
| Background JIT barrier | 0.63 s | the same 2,098-operation testbench process compiled 6 times (once per `rs_thru` instance) |
| Run | 5.03 s | generated kernel code 48%, kernel runtime 19%, host scheduler, runtime and testbench code 31% |

**What the run costs, and what has been tried.** The kernel executes 2.74G KIR operations
at about 1 ns each.
- Combinational partitions (about 800 operations) re-run whole when any input changes.
- Per-member dirty bits skipped 38% of member runs but made the run slower at O0.
- The O1/O2 backend saves run time but costs more in compile time than it saves.
- Keeping registers in SSA gave nothing.
- IR pipelines beyond `sroa` gave nothing or lost time (`early-cse`, `simplifycfg`; `instcombine` costs +1.3 s).
- The IR is about 25% register-file traffic, plus binding loads for every signal access
  (templates are shared across instances).

**Conclusion.** Generated code and kernel runtime alone are about 3.3 s, already above the
3 s simulation budget. Removing every startup and host cost would still miss the gate.
Reaching ≤3 s needs a change of approach, not more tuning. Candidates:

1. **A known-value (2-state) fast path with a 4-state guard.** Generate each template
   assuming known inputs, and fall back to the current 4-state code when a guard sees X/Z.
   This roughly halves the operations of 4-state logic. Estimated −0.7 to −1.2 s of run.
2. **A flat kernel image produced at elaboration.** Members, slots and KIR would be
   loaded directly, and kernel members would never enter the host interpreter. This
   removes most of artifact decode, interpreter population, kernel construction and
   start. Estimated −1.5 to −2 s.
3. **A v4 behavioral tier** for testbench processes (KIR resumable functions, compiled per
   template). This removes the 0.63 s barrier and most host run costs. Estimated −1.5 to
   −2 s.
4. **Code generation for the kernel loop itself** (plan §3.3: no host dispatch per
   partition). Estimated −0.5 s of kernel runtime.

**Owner decision (2026-10-07).** Item 1 first, because it is the cheapest test of
whether ≤3 s is reachable at all; then items 2–4. Item 1 was then measured and rejected
(see the log). For the testbench side, the owner chose to build the v4 behavioral tier
(item 3) rather than make targeted fixes to the old host JIT, which D1 freezes.

With 2 to 4 together, Verilog simulation is estimated at about 4–5 s and total at about
8–9 s. That meets the Phase 1 total (≤10 s) but not simulation ≤3 s. Item 1 would be
needed as well to approach 3 s. D6 (Verilog ≤5.6 s total) also needs elaboration (3.45 s
now) cut by more than half.

## 4b. Behavioral tier design (plan §3.5, owner decision 2026-10-07)

Goal: testbench processes run inside the kernel as compiled resumable code, so the
throughput designs need no host processes. This removes the host JIT barrier, the
host scheduler and commit paths, and array access through callbacks.

**Admission.** A SystemVerilog process that is not combinational, sequential or once
becomes a `behavioral` member when every operation is supported. The supported set is
the existing kernel set plus:
- `WaitFor`, `WaitOn`, `WaitSensitivity`, `WaitForever`;
- `Fork`/`ForkEnd`, `Call`/`Return`, `CallableFramePush`/`Pop`;
- `Display`, `FormatDisplay`, `StringDisplay`, `TimeDisplay`;
- string constants and copies, `PlusArgSelect`, `Halt`, `Stop`.

The new kind stays opt-in behind `FSIM_STATIC_KERNEL_BEHAVIORAL=1` until it reaches
parity.

**Threads.** A behavioral member owns registers, string registers and container
registers. It runs as one or more threads, each with its own pc, call stack and
callable-frame stack. Fork children are threads of the same member that share its
registers, matching the reference `inherit_fork_program` and shared-frame semantics.

**Suspension.**

| Operation | Effect |
|---|---|
| `WaitFor` | kernel timer queue (time, sequence, thread) |
| `WaitOn` | dynamic waiters on kernel slots or host inputs, with edge filters and an optional timeout |
| `WaitSensitivity` | the member's static sensitivity |
| `WaitForever` | parks the thread |
| `Halt` / `ForkEnd` | ends the thread and reports to its fork join |
| `Fork` | spawns child threads; the parent continues per its join kind |
| `Stop` | requests the host stop exactly as the reference does |

**Scheduling.** Ready threads run in the Active step together with triggered sequential
members. Blocking writes are visible at once and settle combinational partitions after
the thread suspends. Nonblocking writes use the NBA queue. `$display` goes through the
host output hook, using the same formatter as the reference (`make_formatted_output`).
After each activation the kernel arms one host timer for its earliest pending wake time.

**Execution.** Stage 1 interprets threads with `step_generic` for correctness. Stage 2
compiles behavioral templates to KIR/LLVM: a `suspend` instruction, an entry switch over
resume points (like the return-target dispatch), and `KOp::generic` for the
runtime-library operations. The six identical testbench instances then share one
template, compiled once.

**Verification.** A new test, `tests/app/static_kernel_behavioral_test.cpp`, compares
kernel and reference transcripts on both engines. The design uses delay clocks,
`repeat` and `@(edge)`, `wait(cond)`, fork/join, automatic tasks and functions, arrays,
formatted `$display` and `$finish`. After that, the round-2 fixtures must stay exact
with the behavioral tier enabled.

## 4c. Mixed-language kernel design (owner direction 2026-10-07: VHDL and Verilog in one kernel)

Today a kernel runs in one language mode. On mixed, the SystemVerilog testbench stays on
the host, which costs the host JIT barrier (0.87 s), host process execution and
boundary traffic.

**Reference interleaving** (the scheduler's slot loop):
- Pending SystemVerilog regions always run before the generic (VHDL) phases continue.
- A SystemVerilog-origin wakeup stays in the current delta.
- A SystemVerilog process woken by a VHDL update is requeued in the next delta's generic
  active phase, so it runs after that delta's VHDL processes and before its update.
- A VHDL process woken by any change runs in the next delta.

**Kernel delta k:**
1. Drain pending SystemVerilog work.
2. Run the VHDL members triggered for round k (writes deferred). The SystemVerilog
   threads and members woken by round k−1's commit become ready.
3. Drain SystemVerilog again. VHDL members woken by these writes join round k+1.
4. Commit the VHDL round. VHDL readers join round k+1; SystemVerilog readers and waiters
   are held for step 2 of delta k+1.

**Mechanics:**
- Every member carries its language.
- Slots of Logic9 signals have four planes; SystemVerilog code reads them through the
  coercing `load_slot9`.
- VHDL round writes and SystemVerilog NBAs use separate queues.
- Publication uses the writer's domain.
- The mode was opt-in (`FSIM_STATIC_KERNEL_MIXED=1`) until the mixed fixtures reached
  parity, both against Vivado and in HDL report counts. It became the default on
  2026-10-07 (`FSIM_STATIC_KERNEL_MIXED=0` disables it).

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

**Current exit criteria (2026-10-08, plan §9 D6).**
- The geometric mean of the per-case e2e speedups must be at least 3×.
- In every case, the simulate phase must be at least 3× faster than `xsim -R`.

Simulate phase with `--aot` (2026-10-08, after `201fb59f`):

| Case | Simulate | Target | Load | Kernel setup | Run (incl. time 0) |
|---|---:|---:|---:|---:|---:|
| mixed_throughput | 2.15 s | 2.10 s | 0.11 | 0.28 | 1.83 |
| mixed_codec | 3.2 s | 2.25 s | 0.42 | 0.58 | 2.12 |
| original_codec | 3.6 s | 2.58 s | 1.0 | 0.6 | 1.9 |
| original_throughput | 2.37 s | 2.62 s | — | — | — |

Plan of record:
1. **Persist the kernel plan.** AOT stores the `StaticKernelRuntimeSpec`, so simulate
   skips planning and, when it is present, the region-graph rebuild at load. Gain: about
   0.3 s on original_codec and 0.13 s on mixed_codec.
2. **Template-level member compile.** KIR is compiled once per shared body and
   substituted per instance, as specialization recipes already are. Gain: 0.1–0.2 s per
   case.
3. **Host population** of kernel members, which never run on the host. Gain: up to
   0.15 s on original_codec.
4. **Lazy design-IR decode** and the load validations. Gain: about 0.15 s on
   original_codec.
5. **Process-table decode and path restore** at template level. Gain: about 0.2 s on
   original_codec.
6. **mixed_codec's run loop** (2.1 s): commit and scheduling, and generic wide
   operations.
   - An optimizing backend for the many-instance templates would cut it by about 0.5 s,
     but costs about 11 s of AOT codegen. It is off by default
     (`FSIM_STATIC_KERNEL_OPT_MAX_SIZE`).

The D6 throughput criterion is met on both throughput cases (§2). Next:
1. Assemble the wider corpus (plan Phase 0) and measure D6's first clause on it.
2. D6's second clause is deferred to Phase 6 (§4, Verilator reference).
3. Qualify the Windows mimalloc path (redirect DLL) on a Windows host.
4. Widen the mixed margin (3.1–3.3× xsim):
   - VHDL instance elaboration;
   - the workspace selection's second object load;
   - the kernel's per-write commit cost.

The older notes below describe the state before the D6 work.

Measured state (2026-10-07, `rep.py`, CPU 9, Verilog, before the D6 work):

| Item | Time |
|---|---:|
| Artifact load (decode 0.35 s: process table 70 ms, path restore 75 ms, region graph 120 ms) | 0.53 s |
| `plan_static_kernel` | 0.06 s |
| Interpreter population (85k host processes) | 0.09 s |
| Kernel member setup (op expansion 74 ms) | 0.12 s |
| KIR compile | 0.10 s |
| Partitions | 0.05 s |
| Codegen (cold 205 ms, hot 115 ms) and canonicalization | 0.40 s |
| Interpreter start | 0.05 s |
| Kernel run | 1.35 s |

1. **Elaboration** (2.85 s each case) is now the largest phase.
   - It is roughly a third of each total, and the plan budgets ≤1.0 s.
   - Known items: generate-loop lowering of 35k concurrent statements (no template reuse
     across genvar overlays), runtime-state encoding, the region graph and driver
     inventory, and hierarchy paths.
2. **Mixed simulation**: codegen of 663 VHDL-heavy templates (about 0.55 s, all cold) and
   the VHDL delta run. Local registers do not apply to VHDL templates yet: deoptimization
   resumes from the register file. Flushing locals before each deopt exit would let them.
3. **Per-instance work** at load and setup: process-table decode, path restore,
   population, member op expansion, KIR compile per member. Template-level processing
   (one pass per body, instances as binding rows; plan §3.7) removes most of it.
4. **Runtime-state format**: each instance's debug scopes and signal operands are
   materialized as full operation overrides (about 390k). A compact encoding of the
   in-memory overrides would cut decode and encode time.
5. **Kernel run**: notification and scheduling are about 35% of the run, generated code
   about 60%.
