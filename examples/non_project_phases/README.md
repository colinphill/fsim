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

## Add an incrementally compiled SystemC library

SystemC uses a separate host-native object and link pair. The three-language
example can be scripted from the repository root without reading its manifest:

```sh
FSIM=build/llvm22-ninja-debug/fsim
mkdir -p /tmp/fsim-three-language

"$FSIM" compile --lang vhdl --standard 2008 --library models \
  --output /tmp/fsim-three-language/vhdl.fsimobj \
  examples/three_language_hierarchy/logic_stage.vhd

"$FSIM" compile --lang systemverilog --standard 2017 --library work \
  --output /tmp/fsim-three-language/sv.fsimobj \
  examples/three_language_hierarchy/three_language_tb.sv

"$FSIM" systemc compile \
  --output /tmp/fsim-three-language/bridge.fsimscobj \
  examples/three_language_hierarchy/mixed_bridge.cpp

"$FSIM" systemc link \
  --object /tmp/fsim-three-language/bridge.fsimscobj \
  --library models \
  --output /tmp/fsim-three-language/models.fsimscplugin

"$FSIM" elaborate \
  --object /tmp/fsim-three-language/vhdl.fsimobj \
  --object /tmp/fsim-three-language/sv.fsimobj \
  --systemc-plugin /tmp/fsim-three-language/models.fsimscplugin \
  --search-library models --top demo=sv:work.three_language_tb \
  --output /tmp/fsim-three-language/design.fsimdesign

"$FSIM" simulate \
  --design /tmp/fsim-three-language/design.fsimdesign \
  --engine compiled --cache /tmp/fsim-three-language/native-cache \
  --trace /tmp/fsim-three-language/three-language.vcd
```

Each C++ translation unit gets its own `.fsimscobj`; repeat `systemc compile`
for additional files and pass the ordered objects to one `systemc link`.
Unchanged translation units are independently reusable in project mode. The
link phase loads the candidate image before publication and records the sorted
factory names and parameter schemas. Macro exports may span translation units
and synthesize the sole current `fsim_plugin_init_v1` ABI entry point. A low-
level source may implement that same current entry point directly, but it is
mutually exclusive with macro exports and is not the removed SystemC facade.

The final format-2 `.fsimdesign` embeds only the selected `models` plug-in.
After elaboration, the original C++ source, `.fsimscobj`, and
`.fsimscplugin` may all be moved away. Standalone simulation validates the
embedded checksums and exact host ABI, reconstructs the SystemC hierarchy by
stable path, reconnects it to the shared runtime, and preserves lifecycle,
debugger, callback, and trace behavior. Because that image is native, move the
design only to a host with the recorded compatible compiler target and fsim
runtime/SystemC ABI.
