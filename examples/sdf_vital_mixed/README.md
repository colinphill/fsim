<!-- SPDX-License-Identifier: Apache-2.0 -->
# Mixed VHDL/VITAL SDF example

This independently authored example connects a VHDL timing cell to a
SystemVerilog boundary and applies SDF 4.0 annotation.

- `vital_cell.vhd` supplies the VHDL timing target.
- `boundary.sv` supplies the SystemVerilog endpoint and `mixed_top` root.
- `mixed.sdf` supplies an exact min/typ/max `IOPATH`.
- `annotate.tcl` demonstrates bounded Tcl configuration and summary access.

From this directory:

```sh
fsim compile --library work vital_cell.vhd
fsim compile --library work boundary.sv
fsim elaborate work.mixed_top --snapshot timed --sdf mixed.sdf \
  --sdf-root mixed_top --sdf-cell 'mixed_top.u_vital' \
  --delay-mode typ --sdf-report-limit 32
fsim simulate --snapshot timed
```

Interpreter and LLVM execution retain the same effective timing identity in
snapshots, native caches, and checkpoints. The same logical annotation controls
are available through Tcl and the C/C++ API. See the [SDF guide](../../docs/sdf.md)
for supported VITAL models, mixed-language boundaries, and failure behavior.
