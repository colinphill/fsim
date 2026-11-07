<!-- SPDX-License-Identifier: Apache-2.0 -->
# Mixed VHDL/VITAL SDF example

This independently authored example connects a VITAL cell, written in VHDL, to
a SystemVerilog boundary and applies SDF 4.0 annotation to its timing generic.

- `vital_cell.vhd` supplies the VITAL model `vital_core` (a Level 0 entity with
  the backannotation timing generic `tpd_a_z` and a Level 1 architecture) and
  the VHDL cell `vital_cell` that holds it.
- `boundary.sv` supplies the SystemVerilog endpoint and `mixed_top` root.
- `mixed.sdf` supplies an exact min/typ/max `IOPATH`, which the SDF map of
  IEEE 1076.4-2000 5.2.7.1.2 turns into the generic `tpd_A_Z`.
- `bench.sv` and `bench.sdf` drive `mixed_top` and annotate it from `mixed_bench`.
- `annotate.tcl` shows that a Tcl session refuses to annotate a VITAL cell.

From this directory:

```sh
fsim compile --library work vital_cell.vhd
fsim compile --library work boundary.sv
fsim elaborate work.mixed_top --snapshot timed --sdf mixed.sdf \
  --sdf-root mixed_top --sdf-cell 'mixed_top.u_vital.u_core' \
  --delay-mode typ --sdf-report-limit 32
fsim simulate --snapshot timed
```

To see the annotated delay, compile `bench.sv` too and elaborate it:

```sh
fsim compile --library work bench.sv
fsim elaborate work.mixed_bench --snapshot benched --sdf bench.sdf \
  --sdf-root mixed_bench --delay-mode typ
fsim simulate --snapshot benched
```

`z` follows `a` after the typical `2 ns` of `IOPATH A Z (1:2:3)`, not after the
`1 ns` default of the generic.

The generics of a VITAL model are constants of elaboration, so the SDF file is
given to `fsim elaborate`, which elaborates the design again with the
annotated generic values; `fsim simulate --sdf` and the Tcl and API sessions
refuse a file that names VITAL cells. Interpreter and LLVM execution retain the
same effective timing identity in snapshots, native caches, and checkpoints.
See the [SDF guide](../../docs/sdf.md) for supported VITAL models,
mixed-language boundaries, and failure behavior.
