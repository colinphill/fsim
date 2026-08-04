<!-- SPDX-License-Identifier: Apache-2.0 -->
# Manifest-free compile, elaborate, and simulate

This tutorial runs the mixed VHDL/SystemVerilog vertical slice as three
explicit, restartable phases. It deliberately does not load `fsim.toml`.

From this directory, set `FSIM` to the simulator executable and create the
consumer directories:

```sh
FSIM=../../build/llvm22-ninja-debug/fsim
mkdir -p work files
```

Compile the VHDL and SystemVerilog compilation units independently:

```sh
"$FSIM" compile --lang vhdl --standard 2008 --library work \
  --output work/counter.fsimobj ../vertical_slice/counter.vhd

"$FSIM" compile --lang systemverilog --standard 2017 --library work \
  --compilation-unit source-set --output work/testbench.fsimobj \
  ../vertical_slice/tb.sv ../vertical_slice/sv_child.sv
```

Each `.fsimobj` contains relocated source snapshots and portable owning units.
It is checksummed, transactionally installed, read-only, and never overwritten.

Elaborate the ordered object list. The SystemVerilog `counter` instance
automatically resolves the uniquely named VHDL entity in logical library
`work`; no binding manifest is needed.

```sh
"$FSIM" elaborate \
  --object work/counter.fsimobj \
  --object work/testbench.fsimobj \
  --top demo=sv:work.tb \
  --output work/vertical.fsimdesign \
  --delay-mode typ --seed 1
```

The original sources and both `.fsimobj` directories may now be moved away.
The `.fsimdesign` contains the complete checksummed runtime, semantic/debug,
and DesignIR projections needed for simulation.

Run with explicit native-cache, HDL file-I/O, and filtered-trace paths:

```sh
"$FSIM" simulate --design work/vertical.fsimdesign \
  --engine compiled --cache work/native-cache --file-root files \
  --trace work/vertical.vcd --trace-filter 'demo.*'
```

Use `--engine interpreter` for the reference evaluator or `--engine debug` for
source-instrumented O0 execution. `--duration`, `--max-deltas`, and `--seed`
may be supplied at simulation time. Delay selection belongs to elaboration, so
`simulate --delay-mode` is a compatibility assertion and must match the design.
All derived output stays in the explicit consumer paths; neither artifact tree
is modified.
