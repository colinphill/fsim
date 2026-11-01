<!-- SPDX-License-Identifier: Apache-2.0 -->
# Unified coverage

fsim v3 records code, SystemVerilog functional, and PSL coverage in separate
namespaces of one bounded `.fsimcov` database. Coverage is opt-in, has no
instrumentation cost when disabled, and never invents a score across metric
families.

## Enable and save coverage

Enable code coverage while compiling and elaborating a workspace snapshot:

```sh
fsim compile --code-coverage --library work counter.sv tb.sv
fsim elaborate work.tb --code-coverage
fsim simulate
```

The `--code-coverage` option is also accepted by snapshot consumers, but cannot
add instrumentation to a snapshot elaborated without coverage. Re-elaborate
that snapshot with coverage enabled. Older snapshots marked as enabled but
lacking a point inventory also require re-elaboration.

`fsim simulate --coverage-output FILE` saves the database at the end of the
run for any design, SystemVerilog, VHDL or mixed. `$coverage_save` remains
available from SystemVerilog, for example
`$coverage_save(`SV_COV_STATEMENT, "run.fsimcov")`. Database replacement is
atomic. A corrupt, incompatible, or resource-exceeding input fails without
replacing a valid destination.

### What is counted

Discovery runs on the compiled HIR of every elaborated SystemVerilog module
and VHDL architecture. Each elaborated instance has its own counters, and
points that a run never reaches, including paths removed by constant
specialization, stay in the inventory with zero hits. All counting runs as
SimIR, so the interpreter and the v4 kernel count the same.

| Metric | SystemVerilog | VHDL |
| --- | --- | --- |
| `statement` | procedural statements, variable initializers, each `assign` evaluation | process statements, each evaluation of a concurrent signal assignment |
| `branch` | `if`/`else` outcomes (an implicit `else` included), `case` arms (an implicit no-match arm without `default`), both outcomes of each `?:` in an `assign` | `if`/`elsif`/`else` outcomes, `case` arms, the alternatives of conditional and selected assignments |
| `condition` | true and false of each operand of `&&`, `\|\|`, `!` in a decision | true and false of each operand of `and`, `or`, `nand`, `nor`, `xor`, `xnor`, `not` on BOOLEAN, BIT and STD_ULOGIC |
| `expression` | one row per reachable short-circuit evaluation path of a decision | the same |
| `toggle` | 0→1 and 1→0 per bit of ports, nets and variables | the same, per bit of ports and signals |
| `fsm-state`, `fsm-transition` | inferred enumeration machines and hinted machines | the same |

Decisions are `if` conditions, loop conditions and VHDL conditional
assignment conditions. A decision whose operands could have an effect when
evaluated again (an update, a non-pure call) is not decomposed. Operands are
named by their source span; a decision with repeated operand spans, more than
16 operands or more than 64 rows is skipped. An unknown operand ends the
evaluation walk without a count. STD_ULOGIC `H` counts as true.

A `?:` outcome is counted when its condition is side-effect free; an unknown
condition counts in neither outcome. Nested `?:` operators in an outcome are
counted when that outcome is taken.

Toggle coverage counts a transition only between known values: a change to or
from X or Z counts in neither direction, and STD_ULOGIC reads through
TO_X01 (`L` is 0, `H` is 1). The value at time 0 is the baseline. SV integer
types such as `int` are packed and are covered bit by bit. Reals, VHDL
integers, access and physical types, VHDL enumerations other than BOOLEAN and
character types, and objects wider than 4096 bits are not covered. Memories
(SV unpacked arrays, VHDL arrays of words or of more than one dimension) are
excluded by default and appear as one exclusion row with the reason
`memory: arrays of words are excluded from toggle coverage by default`.

### State machines

A state machine is a `case` over a whole state object whose arms assign
states to the object or to its next-state partner (an object of the same
enumeration type, or the hinted next-state object).

- The states of an enumeration machine are its literals. VHDL character types
  and BOOLEAN are values, not states.
- Any other object is a machine only with a hint; its states are the named
  constants (or VHDL string literals) its case arms choose.
- A legal-state hint restricts the states to the names it lists; a value
  outside them is in no state. Each name must be a literal or case choice of
  the object. A VHDL
  STD_LOGIC_VECTOR state compares elements exactly: a value with an `H`, `L`,
  `X` or other non-`0`/`1` element is in no state.
- The legal transitions are S→T where the arm for S assigns T, following
  both outcomes of SV `?:`.
- Transitions are counted when the state object changes, so a self
  transition is not observable and has no point.
- Each machine has one `fsm-other-transition` row: changes between two states
  that no arm assigns. It counts hits but is reported as excluded with the
  reason `informational: changes between states that no case arm assigns`, so
  it never counts toward totals or thresholds.
- At most 64 states and 64 bits are covered.

Hints name the current-state object, optionally its next-state partner and
the legal states:

```systemverilog
(* fsm_current_state="state", fsm_next_state="next", fsm_legal_states="IDLE,RUN" *)
module ctrl(...);
```

```vhdl
attribute fsm_current_state : string;
attribute fsm_current_state of state : signal is "true";
attribute fsm_next_state of state : signal is "next_state";
attribute fsm_legal_states of state : signal is "idle,run";
```

A project manifest can also give hints in `config.coverage.fsm_hints`
(instance, current state, next state, legal states). An invalid hint is
reported as FSIM-COV-026.

### Exclusions

Source controls use independently authored comments:

```systemverilog
initial begin
  // fsim coverage off metric=statement reason="generated setup"
  adapter = 0;
  // fsim coverage on metric=statement
end
```

VHDL uses `-- fsim coverage off ...` and `-- fsim coverage on ...`. A point in
an excluded region has no counter and is reported as an exclusion row with
the comment's reason. Metrics are excluded independently.

A project manifest can exclude points by source, hierarchy, object and
metric:

```toml
[[coverage.exclude]]
hierarchy = "tb.u_*"
object = "status*"
metric = "toggle"
reason = "unused status plumbing"
```

Every rule needs a metric (or `all`) and a reason, and its selectors are
conjunctive. Source patterns match the source's logical path, hierarchy
patterns the instance path, and object patterns the toggle or state object's
name. A selected point still counts its hits, but the report shows them as
excluded hits with the rule's reason, so they never count toward totals or
thresholds.

Discovery rejects ambiguous duplicate point identities, including macro
expansions whose distinct executable statements share the same physical source
span. It does not silently combine those execution sites into one counter.
Objects declared together (`signal a, b : t;`) share a span and are told
apart by their position.

SystemVerilog coverpoints/crosses and PSL directives/properties occupy their
own namespaces.

## Merge, report, and gate CI

Strict merging requires the same design model:

```text
fsim coverage merge first.fsimcov second.fsimcov --output merged.fsimcov
```

Use `--partial` only to merge unchanged point identities from a changed model.
Conflicting identities, duplicate runs, or incompatible namespaces reject the
whole operation.

Reports default to text and can be written as text, HTML, full-fidelity JSON,
LCOV, or Cobertura:

```text
fsim coverage report merged.fsimcov --format html --output coverage.html
fsim coverage report merged.fsimcov --format json --output coverage.json
fsim coverage report merged.fsimcov --threshold statement=90
```

Thresholds are per metric family. An unmet threshold returns exit status 4;
usage errors return 2 and operational failures return 1. LCOV and Cobertura
are deliberately lossy projections of statement, explicit-line, and branch
coverage. Use JSON when every namespace, exclusion, and instance result must
be retained.

## Standard control APIs

SystemVerilog exposes the standard coverage constants plus
`$coverage_control`, `$coverage_get`, `$coverage_get_max`, `$coverage_merge`,
and `$coverage_save`. `SV_COV_STATEMENT`, `SV_COV_TOGGLE` and
`SV_COV_FSM_STATE` select the statement, toggle and FSM state points of the
selected modules or instances: `$coverage_get` returns the covered points,
`$coverage_get_max` all counted points, and `$coverage_control` starts, stops,
resets or checks their counters. Excluded points are not counted. Merge and
save act on the whole database for any of these types. `SV_COV_ASSERTION`
returns `SV_COV_NOCOV`; assertion coverage is available through the VPI
coverage service. The VPI coverage service uses the same live model,
selection rules, stable identities, and database transactions. VHDL PSL bins
are published through the same database without being converted into
SystemVerilog bins.

See the [diagnostic catalog](diagnostics.md), [SystemVerilog VPI guide](systemverilog-vpi.md),
and [VHDL-2019 guide](vhdl-2019.md) for the exact related surfaces.
