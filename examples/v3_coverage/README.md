<!-- SPDX-License-Identifier: Apache-2.0 -->
# v3 unified coverage example

Run this independently authored SystemVerilog-2023 example from this directory:

```sh
fsim compile counter.sv coverage_tb.sv
fsim elaborate work.coverage_tb
fsim simulate
```

The directory's `fsim.tcl` runs before each command and selects
SystemVerilog-2023, enables code coverage for compilation and elaboration, and
limits delta cycles to 1000. The same run without it is:

```sh
fsim compile --no-config --lang systemverilog --standard 2023 --code-coverage \
  counter.sv coverage_tb.sv
fsim elaborate --no-config work.coverage_tb --code-coverage
fsim simulate --no-config --max-deltas 1000
```

The covergroup contributes functional coverage in its separate runtime
namespace. The [unified coverage guide](../../docs/code-coverage.md) describes
database save, merge, report, and CI thresholds, and the
[configuration guide](../../docs/tcl.md#configuration) lists every key.
