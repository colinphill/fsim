<!-- SPDX-License-Identifier: Apache-2.0 -->
# Workspace compile, elaborate, and simulate

This tutorial runs the mixed VHDL/SystemVerilog vertical slice as three
restartable phases. The current directory is the workspace; fsim creates and
manages `.fsim` automatically.

From this directory, select an executable and compile the sources:

```sh
FSIM=../../build/dev/fsim
"$FSIM" compile --lang vhdl --standard 2008 --library work \
  ../vertical_slice/counter.vhd
"$FSIM" compile --lang systemverilog --standard 2017 --library work \
  --compilation-unit source-set \
  ../vertical_slice/tb.sv ../vertical_slice/sv_child.sv
"$FSIM" library objects work
```

The catalog at `.fsim/libraries/work/library.sqlite3` connects named units to
managed artifacts. Each primary HDL unit has its own artifact where practical;
associated package classes and supporting definitions travel with their owner.
Recompile a source with the same command to replace its previous definitions.
A failed compile preserves the prior published library.

Elaborate by top name, then simulate the default snapshot:

```sh
"$FSIM" elaborate demo=work.tb --delay-mode typ --seed 1
"$FSIM" simulate --engine compiled --trace vertical.vcd --trace-filter 'demo.*'
```

The SystemVerilog `counter` instance resolves the VHDL entity in `work`
without a language qualifier or binding file. The snapshot is published under
`.fsim/snapshots/default`; it retains the compiled runtime, semantic/debug,
and DesignIR state needed for simulation.

Use `--snapshot NAME` on both commands to keep a named snapshot:

```sh
"$FSIM" elaborate demo=work.tb --snapshot regression
"$FSIM" simulate --snapshot regression --engine interpreter
```

The original sources may now be moved away. Recompiling or deleting library
objects also leaves existing snapshots usable. Use `--engine debug` for O0
instrumentation, and `--duration`, `--max-deltas`, or `--seed` for simulation
controls. Delay selection belongs to elaboration; a simulation-time
`--delay-mode` must agree with the snapshot.

Progress defaults to normal. Add `-q` to suppress compile/elaborate progress
or `-v` for more detail. Diagnostics remain available. Generated caches stay
under `.fsim/cache`; trace and HDL file-I/O outputs use their requested paths.

## Add an incrementally compiled SystemC library

Run this part from `examples/three_language_hierarchy`, with fsim on `PATH`:

```sh
fsim compile --lang vhdl --standard 2008 --library models logic_stage.vhd
fsim compile --lang systemverilog --standard 2017 --library work \
  three_language_tb.sv
fsim systemc compile --library models mixed_bridge.cpp
fsim systemc link --library models
fsim elaborate demo=work.three_language_tb --search-library models \
  --snapshot mixed
fsim simulate --snapshot mixed --engine compiled --trace three-language.vcd
```

Each C++ translation unit gets one managed object. Repeat `systemc compile`
for changed translation units, then link the library again. The link phase
loads the candidate image before publication and records its factories and
parameter schemas. Macro exports can span translation units and synthesize the
sole current `fsim_plugin_init_v1` ABI entry point. A low-level source can
implement that entry point directly, but cannot mix it with macro exports.

The snapshot retains the selected plugin. The original C++ sources and
library objects are not needed by later simulation. Because that payload is
native, simulation requires a compatible host and fsim/SystemC ABI. fsim
validates its checksums, reconstructs the hierarchy by stable path, and
reconnects it to the shared runtime.

See the [workspace guide](../../docs/workspace-mode.md) for compiled package
reuse, mapped libraries, object deletion, and migration from older commands.
