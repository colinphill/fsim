<!-- SPDX-License-Identifier: Apache-2.0 -->
# v3 unified coverage example

Run this independently authored SystemVerilog-2023 example from this directory:

```sh
fsim compile --lang systemverilog --standard 2023 --code-coverage \
  counter.sv coverage_tb.sv
fsim elaborate work.coverage_tb --code-coverage
fsim simulate --max-deltas 1000
```

Code coverage is enabled explicitly. The covergroup contributes functional
coverage in its separate runtime namespace. The
[unified coverage guide](../../docs/code-coverage.md) describes database save,
merge, report, and CI thresholds. The historical `fsim.toml` fixture is not
loaded by workspace commands.
