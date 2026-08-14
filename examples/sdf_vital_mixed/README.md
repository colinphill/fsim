<!-- SPDX-License-Identifier: Apache-2.0 -->
# Mixed VHDL/VITAL SDF example

This small producer-independent example shows a VHDL timing cell connected to
a SystemVerilog boundary and annotated by SDF 4.0. The same logical controls
apply from a project, CLI, Tcl, C/C++ API or a manifest-free simulation phase.

The files are intentionally clean-room and Apache-2.0-owned:

- `vital_cell.vhd` is the VHDL timing target;
- `boundary.sv` supplies the adjacent SystemVerilog endpoint;
- `mixed.sdf` supplies an exact min/typ/max `IOPATH`; and
- `annotate.tcl` demonstrates bounded Tcl configuration and summary access.

From a project that compiles the two HDL sources and selects `mixed_top` as a
root, the equivalent CLI shape is:

```sh
fsim run --project fsim.toml --sdf mixed.sdf --sdf-root mixed_top \
  --sdf-cell 'mixed_top.u_vital' --delay-mode typ --sdf-report-limit 32
```

Interpreter and LLVM, cold/warm/relocated execution and object/design/library/
cache/checkpoint persistence retain the same effective timing identity. Use
the [SDF guide](../../docs/sdf.md) for exact supported VITAL models,
mixed-language boundaries, failure behavior and evidence.
