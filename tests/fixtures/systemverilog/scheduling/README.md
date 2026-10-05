<!-- SPDX-License-Identifier: Apache-2.0 -->
# Verilog and SystemVerilog scheduling witnesses

These small designs isolate the event-region rules needed by fsim's default
IEEE 1800-2023 mode. Most fixtures use SystemVerilog syntax. The
`continuous_active_verilog.v` twin is parsed as Verilog-2005 and checks that
the legacy source-language mode still places continuous-assignment updates in
Active before a `#0` observer runs in Inactive. The `.v` scheduling twins also
cover implicit expression-port propagation, Active/Inactive/NBA visibility,
and end-of-slot `$strobe` after an NBA-triggered Active round. File extensions
select their respective language modes; these `.v` witnesses use Verilog-2005
syntax only. Their expected transcripts reuse the matching established
SystemVerilog outputs. Independent Verilog-2005 oracle receipts are recorded
separately from the original SystemVerilog control corpus.

Most established files listed below under `expected/` are verified
contract transcripts from Vivado 2025.2.1, including SystemVerilog-only cases,
one mixed VHDL/SystemVerilog boundary, and one inertial-update case. The
multidimensional fixed-array read and fixed-array write cases have measured
external-oracle deviations described below; their expected transcripts remain
fsim contract witnesses pending resolution. The separate
`control/` corpus records the observed interpreter behavior from the frozen
`ede7c24e` control before scheduling corrections for the eight original
SystemVerilog cases. It is intentionally separate from the oracle contract so
a known-divergent control result cannot become expected output.

`ordered_wave_native.sv` extends the corpus with one-, 65-, 129-, 256-, and
1024-bit continuous-assignment members. Each checks known, X, and Z
publication. Its transcript was checked against Vivado 2025.2.1. The LLVM
runner additionally requires the explicit `sv-ordered-wave` profile to show
at least two offered and executed native members.

Run the installed Vivado oracle from any directory with:

```sh
VIVADO_BIN=/opt/eda/AMD/2025.2.1/Vivado/bin KEEP_WORKSPACE=1 \
  sh tests/fixtures/systemverilog/scheduling/run_vivado.sh
```

The runner keeps each raw compile, elaborate, and simulation log under the
printed temporary workspace when `KEEP_WORKSPACE=1`. It removes only known
Vivado banner, run-control, finish, and shutdown lines, then compares every
remaining simulation line exactly with the fixture transcript. An optional
Icarus runner is also provided; Icarus selects SystemVerilog mode with
`-g2012` and compiles the Verilog-2005 twin with `-g2005`. The fsim transcript
test uses the language selected by each source extension and its default
standard selection, with no standard override.

The mixed-language witness has a separate Vivado runner because it compiles
both VHDL and SystemVerilog. Its contract requires the VHDL leaf's two
successive zero-delay projected cycles to remain distinct while the SV
consumer observes the settled value after the mixed-language boundary. Its
transcript is included in the thirteen-case fsim scheduling test:

```sh
VIVADO_BIN=/opt/eda/AMD/2025.2.1/Vivado/bin KEEP_WORKSPACE=1 \
  sh tests/fixtures/systemverilog/scheduling/run_vivado_mixed.sh
```

`inertial_maturation_order.sv` is another verified oracle witness. It verifies
that an inertial transition superseded before maturity is canceled and a
matured zero-delay transition is visible to an Inactive observer in its
timestamp. Its separate runner is:

```sh
VIVADO_BIN=/opt/eda/AMD/2025.2.1/Vivado/bin KEEP_WORKSPACE=1 \
  sh tests/fixtures/systemverilog/scheduling/run_vivado_inertial.sh
```

| Fixture | Rule witnessed | Vivado 2025.2.1 oracle | Frozen `ede7c24e` control (or labeled current pre-fix result) |
|---|---|---|---|
| `continuous_active.sv` | A continuous assignment settles in Active before a `#0` observer runs in Inactive. | `DIRECT value=1`, exit 0 | `DIRECT value=0`, exit 0 |
| `continuous_active_verilog.v` | The same Active-before-Inactive rule through the Verilog-2005 `.v` frontend path. | `DIRECT value=1`, exit 0; targeted XSim run outside the sandbox on 2026-10-04 | Not in the frozen control corpus; current pre-fix interpreter, LLVM O0, and LLVM O2 each print `DIRECT value=0`, exit 0 |
| `ordered_wave_native.sv` | 1-, 65-, 129-, 256-, and 1024-bit continuous assignments preserve known, X, and Z values through an ordered Active wave. | `ORDERED native publication PASS`, exit 0 | Not captured in the frozen control corpus |
| `implicit_port_active.sv` | Input and output expression port connections propagate as implicit continuous assignments before Inactive. | `PORT input=1 output=1`, exit 0 | `PORT input=1 output=0`, exit 0 |
| `inactive_nested_delta.sv` | A producer and observer may order either way in one Inactive batch; after a nested `#0`, the intervening Active work has drained. | `INACTIVE race=accepted`; `INACTIVE nested=settled`, exit 0 | `INACTIVE race=accepted`; fails with `continuous assignment did not settle before nested Inactive`, exit 1 |
| `active_inactive_nba.sv` | Blocking Active writes are visible before Inactive; NBA writes are still old in Inactive and visible at the end of the time slot. | All four expected lines, exit 0 | Matches oracle, exit 0 |
| `nba_batch_before_active.sv` | Active processes triggered by an NBA do not run until all NBA updates already queued for that batch have committed. | `NBA batch sampled_b=1 b=1`, exit 0 | Matches oracle, exit 0 |
| `derived_clock_nba.sv` | A clock edge propagated by `assign` samples data before a same-slot NBA changes it. | `DERIVED sampled=1 data=0`, exit 0 | `DERIVED sampled=0 data=0`, exit 0 |
| `strobe_end_of_slot.sv` | `$strobe` observes the final value after an NBA triggers another Active/NBA round in the same time slot. | `STROBE value=1`, exit 0 | `STROBE value=0`, exit 0 |
| `monitor_once_per_slot.sv` | `$monitor` emits one final-value line after Active and NBA-triggered value changes in one time slot. | `MON value=0` at registration and one final `MON value=1`, exit 0 | `MON value=0` twice, then `MON value=1`, exit 0 |
| `mixed_domain_boundary.sv` | Two VHDL projected cycles remain distinct; the SV consumer sees the settled boundary value. | `MIX inactive result=0`; `MIX settled result=1 event=1`, exit 0 | Not captured in the frozen control corpus |
| `inertial_maturation_order.sv` | A superseded inertial write is canceled, while a matured write is visible to Inactive. | `INERTIAL canceled=0`; `INERTIAL matured=1`, exit 0 | Not captured in the frozen control corpus |
| `reactive_reinactive_re_nba.sv` | A program's delayed NBA remains pending through Reactive and Re-Inactive, then `$strobe` observes its Re-NBA value. | `PROGRAM reactive value=0`; `PROGRAM re-inactive value=0`; `STROBE value=1`, exit 0 | Not captured in the frozen control corpus |
| `named_event_triggered_nba.sv` | A nonblocking named-event trigger is invisible at request time, visible to the awakened waiter through the rest of the time slot, and clear in the next slot. | `REQUEST triggered=0`; `INACTIVE_REQUEST triggered=0`; `WAKE triggered=1`; `INACTIVE triggered=1`; `NEXT_TIME triggered=0`, exit 0 | Not captured in the frozen control corpus |
| `multidimensional_fixed_read.sv` | Fixed rank-two/rank-three reads preserve index width and signedness, return element defaults for X/Z and invalid indices, evaluate side-effecting dimensions once even when a coordinate is invalid, and keep dynamic continuous/`always_comb` reads sensitive to selected and sibling element changes. | Expected transcript retained; XSim reached `$finish` outside the sandbox and matched 12/14 normalized lines. The X/Z and 64/65-bit invalid-index lines differ. | Captured partial external deviation; Icarus is not installed. |
| `fixed_array_write.sv` | Fixed packed arrays preserve full-width signed/unknown indices for blocking, NBA, local and partial writes. Invalid writes evaluate indices/RHS and leave storage and events unchanged; NBA captures the original index. | XSim reached `$finish` outside the sandbox on 2026-10-04 and matched 8/11 normalized lines. On the `INVALID` line it writes `ff` to the selected matrix element and `line[1]`, and reports an event; `LOCAL` returns `2f` rather than `29`; `READ` returns `ff` rather than `xx` for the wide index. The expected transcript remains unchanged. | Interpreter, LLVM O0, and LLVM O2 scheduling gates pass the expected transcript; external parity is unresolved. Raw XSim logs: `/tmp/fsim-sv-scheduling-vivado.qy1I90/`. |

The additional Verilog-2005 sources are `implicit_port_active_verilog.v`,
`active_inactive_nba_verilog.v`, and `strobe_end_of_slot_verilog.v`. Their
`expected/` files preserve the matching established transcript, and the
interpreter/LLVM runner compiles each with its `.v` extension. Targeted Vivado
2025.2.1 XSim runs outside the sandbox on 2026-10-04 matched all three
expected transcripts byte for byte. Raw compile, elaborate, and simulation
logs are under `/tmp/fsim-sv-scheduling-vivado.cJAgGl/`; the established
SystemVerilog oracle results above remain unchanged.

The multidimensional fixed-array read case has a partial external-oracle
deviation, not full transcript parity. Its expected transcript covers initial X, dynamic
ascending and descending rank-two/rank-three reads, X/Z indices, unsigned 64-
and 65-bit indices whose low 32 bits would otherwise look valid, a negative
signed index, a two-state default, a dynamic index initially X over initialized
elements, continuous and `always_comb` selection after selected and unrelated
sibling changes, and side-effecting index expressions in both dimensions. The
later-dimension side effect after an invalid first coordinate remains covered.
XSim reached `$finish` outside the sandbox; the two differing invalid-index
lines remain under owner triage. The expected transcript is unchanged. The
captured result is in
`build/performance-campaign/simulation-architecture/candidate-feature-next33-20261001/oracle-result.json`.

`control/` contains the normalized semantic output lines and observed simulator
exit status, excluding the command's success footer and generated absolute
source path in the failure diagnostic. The raw control logs are retained under
`build/performance-campaign/simulation-architecture/candidate-m1-validation-20260929/fsim-control-witness-full/`.
Same-region Active and Inactive races must not acquire an exact ordering
assertion unless an independent rule requires it.

The interpreter transcript test is registered as
`fsim.application.systemverilog-scheduling-witnesses`. LLVM builds also register
the `.llvm-O0` and `.llvm-O2` variants. Each uses one fresh workspace per design,
runs compile/elaborate/simulate, and compares every semantic output line against
the oracle transcript. LLVM cases additionally require a positive native
process count from the JIT profile on stderr; a wholly interpreted fallback
fails this gate. Individual unsupported effects may still use checked runtime
boundaries. Direct script invocation accepts `FSIM_SCHEDULING_ENGINE` and
`FSIM_SCHEDULING_OPTIMIZATION`; the default scheduling contract is IEEE 1800-2023.
The named-event case admitted one native process at both O0 and O2; this is
positive design-level admission evidence and does not claim that every fork or
event-wait path executes natively.

### A2 region scheduling boundaries

The three `a2_*.sv` witnesses match Vivado 2025.2.1 and the frozen native184
candidate in interpreter/O0/O2. The full sixteen-case transcript runner passes
all three engines. The new cases admit 14, 2 and 4 native processes respectively
in each compiled run; those are design-level counts, not evidence of hidden
region admission. Raw oracle logs and source/output hashes are recorded in
`candidate-native184-queue-20260930/a2-witness-receipt.json` under the architecture
campaign evidence directory. The initial sandbox XSim attempt reported an
exception and is explicitly excluded; the successful oracle ran outside the
sandbox with the same source inputs.

| Witness | Scheduling distinction | Verified semantic transcript |
|---|---|---|
| `a2_chain_fork_merge.sv` | A two-branch chain feeds a merge while a constant branch's update remains equal; same-process Active markers bracket an unrelated process trigger. Only post-settle counts are checked, so no legal ordering among independent Active tasks is asserted. | `A2 ACTIVE marker=after-source`; `A2 ACTIVE marker=after-unrelated`; `A2 cone visible=1 merged=1 merged_events=1 quiet_events=0 unrelated_runs=1` |
| `a2_active_nba_boundary.sv` | Distinct boundary inputs change in Active and NBA in one time slot. An Inactive read sees the old NBA input; `$strobe` sees the settled result. | `A2 INACTIVE result=0 active=1 nba=0`; `A2 STROBE result=1 active=1 nba=1` |
| `a2_derived_clock_reader.sv` | A two-assignment derived clock drives a clocked data reader; the reader must sample data before a later Inactive update. | `A2 DERIVED sampled=1 data=0 clock=1` |

The first candidate checks the externally visible no-change result and counters;
it cannot externally prove that a hidden region process was not redundantly
resumed. The implementation admission census must separately assert that an
equal private shadow commit does not enqueue its consumers.

### Array range sensitivity oracle

`fsim_sensitivity_ranges.sv` is an A1 admission witness. Vivado 2025.2.1
matches the per-marker event counts in `expected/sensitivity_ranges.json`.
Ignore initial events before the first `BEGIN` and event ordering within each
`BEGIN`/`END` pair. Raw evidence is under
`build/performance-campaign/simulation-architecture/candidate-m2-validation-20260929/vivado-sensitivity/`.
This is external-oracle evidence; optimized fsim qualification remains pending.

Static `always_comb`, `@*`, and explicit slice dependencies ignore changes
outside the selected bits. Explicit whole-element events still fire for other
bits of that element. A dynamic index keeps every array element in the implicit
dependency set, including an element that is not currently selected. Do not
infer an explicit event's sensitivity solely from reads in its process body.

The ascending, wide, and wide-dynamic variants were also verified with Vivado
2025.2.1. They cover ascending source indices, 129-bit elements with a 65-bit
selection, and explicit `@(elements[index])` expression events. The explicit
dynamic expression fires only when the selected value changes; this differs
from the conservative dependency set of an implicit dynamic read. The evidence
and source hashes are in the same M2 directory under
`vivado-sensitivity-extended/summary.json`.

`run_sensitivity.py` checks all four designs, using fresh fsim workspaces and
strict marker-delimited counts. CTests `fsim.sensitivity-ranges.interpreter`,
`fsim.sensitivity-ranges.llvm-o0`, and `fsim.sensitivity-ranges.llvm-o2` exercise
the interpreter and both compiled optimization settings. Compiled tests are
registered when LLVM is enabled; unsupported processes can still use checked
fallback, so these transcript tests alone do not prove native admission.
