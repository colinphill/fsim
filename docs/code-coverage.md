<!-- SPDX-License-Identifier: Apache-2.0 -->
# Unified coverage

fsim v3 records code, SystemVerilog functional, and PSL coverage in separate
namespaces of one bounded `.fsimcov` database. Coverage is opt-in, has no
instrumentation cost when disabled, and never invents a score across metric
families.

## Enable and save coverage

Enable code coverage in a project manifest:

```toml
[coverage]
enabled = true
```

The equivalent command-line override is `--code-coverage`. Statement, branch,
line, condition, expression, toggle, FSM-state, and FSM-transition results use
stable source and instance identities. SystemVerilog coverpoints/crosses and
PSL directives/properties occupy their own namespaces.

SystemVerilog writes a unified snapshot with `$coverage_save`, for example
`$coverage_save(`SV_COV_STATEMENT, "run.fsimcov")`. Database replacement is
atomic. A corrupt, incompatible, or resource-exceeding input fails without
replacing a valid destination.

Source controls use independently authored comments:

```systemverilog
// fsim coverage off metric=statement reason="generated glue"
assign adapter = input_value;
// fsim coverage on metric=statement
```

External exclusions are conjunctive and always require a reason:

```toml
[[coverage.exclude]]
source = "rtl/generated_adapter.sv"
hierarchy = "top.generated*"
metric = "statement"
reason = "generated integration glue"
```

FSM hints use stable elaborated names and do not replace automatic enum/case
inference:

```toml
[[coverage.fsm]]
instance = "top.controller"
current_state = "state"
next_state = "next_state"
legal_states = ["idle", "work", "done"]
```

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
and `$coverage_save`. The VPI coverage service uses the same live model,
selection rules, stable identities, and database transactions. VHDL PSL bins
are published through the same database without being converted into
SystemVerilog bins.

See the [diagnostic catalog](diagnostics.md), [SystemVerilog VPI guide](systemverilog-vpi.md),
and [VHDL-2019 guide](vhdl-2019.md) for the exact related surfaces.
