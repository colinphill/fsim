<!-- SPDX-License-Identifier: Apache-2.0 -->
# Three-language hierarchy tutorial

This workspace example combines SystemVerilog, SystemC, and VHDL. It resolves
children through the parent and search libraries and executes every language
in one HDL-owned hierarchy:

```text
three_language_tb                         SystemVerilog testbench
├── u_bridge                              SystemC MixedBridge factory
│   └── invert                            SystemC method
└── u_vhdl                                VHDL logic_stage(rtl)
```

The SV testbench drives `stimulus`. The SystemC method inverts it into the
HDL-owned `bridge_value` signal, and the VHDL stage inverts it again. The
`observed` value returned to SystemVerilog must therefore equal `stimulus`.
The testbench checks all three transitions and prints `PASS` before calling
`$finish`.

## 1. Build fsim

Run these commands from the repository root. A C++20 compiler is also needed
because `systemc compile` compiles `mixed_bridge.cpp` into a managed native
object before `systemc link` publishes its plugin.

```sh
cmake --preset dev
cmake --build --preset dev --parallel 12 --target fsim
```

For the remaining commands, change to this example directory and put the
built fsim executable on `PATH`, or replace `fsim` with its absolute path.

## 2. Compile, elaborate, and simulate

```sh
fsim check --lang vhdl --standard 2008 logic_stage.vhd
fsim check --lang systemverilog --standard 2017 three_language_tb.sv
fsim compile --library models logic_stage.vhd
fsim compile --library work three_language_tb.sv
fsim systemc compile --library models mixed_bridge.cpp
fsim systemc link --library models
fsim elaborate work.three_language_tb --search-library models
fsim simulate --engine compiled --trace three_language.vcd
```

Compilation publishes each primary HDL unit and each SystemC translation unit
in its managed library. Linking records the exported SystemC factories.
Elaboration resolves both language boundaries and creates the default snapshot.
Use `-v` on compile or elaborate to show more progress detail.

The run should end with:

```text
SV observed 0 after the SystemC -> VHDL path
SV observed 1 after the SystemC -> VHDL path
PASS: SystemVerilog -> SystemC -> VHDL hierarchy
simulation stopped at tick 3, delta 0
```

The workspace's `.fsim` directory contains the `work` and `models` libraries,
the default snapshot, and generated caches. Repeating a compile replaces that
source's definitions; repeating elaboration replaces the selected snapshot.
A previously elaborated snapshot remains usable after library changes.

## 3. Understand target resolution

The SystemVerilog source in `work` names both `mixed_bridge` and `LogicStage`.
They match the public factory alias exported by `SC_FSIM_EXPORT_AS` and the
VHDL entity in logical library `models`. `--search-library models` makes that
library visible for child resolution.

The complete scope contains `work` followed by `models`, so the factory
resolves uniquely:

```text
three_language_tb.u_bridge -> systemc:models.mixed_bridge
three_language_tb.u_vhdl -> vhdl:models.LogicStage(rtl)
```

`SC_FSIM_EXPORT_AS(MixedBridge, "mixed_bridge")` publishes the SystemC
factory. The owning SystemVerilog hierarchy connects its result to the sibling
VHDL instance. VHDL matching is case-insensitive and the unique entity supplies
its `rtl` architecture. SystemC does not construct an HDL proxy child.

Repeat `--search-library` for additional libraries. If a name is missing or
ambiguous in the selected scope, elaboration reports the conflict before
constructing the hierarchy. The top `work.three_language_tb` needs no language
qualifier because its metadata lookup is unambiguous.

The [workspace phase tutorial](../non_project_phases/README.md#add-an-incrementally-compiled-systemc-library)
shows the same flow with a named snapshot.

## 4. Record and inspect VCD or FST

The simulation command above selects VCD tracing. Add
`--trace-filter 'three_language_tb.*'` to restrict the recorded hierarchy.

After simulation, open
`three_language.vcd` in a VCD viewer or
inspect it as text. Useful paths include:

```text
three_language_tb.stimulus
three_language_tb.observed
three_language_tb.bridge_value
three_language_tb.u_bridge.source
three_language_tb.u_bridge.result
three_language_tb.u_vhdl.value
three_language_tb.u_vhdl.result
```

The SystemC boundary ports and VHDL ports change in later deltas than the SV
stimulus, making both crossings and their top-level aliases visible. Adding a
native `sc_signal` beneath `u_bridge` exposes that object through the same
frozen Accellera hierarchy inventory; VCD/FST selection never polls or flattens
the native object into an HDL signal.

Select deterministic FST for the same snapshot:

```sh
fsim simulate --trace three_language.fst --trace-format fst \
  --trace-compression deterministic
```

It writes `three_language.fst`. The FST
contains the same canonical mixed-language hierarchy, aliases, values, and
ordered time changes as the VCD while using the deterministic portable
compression profile. Use an FST-capable viewer or fsim's bounded reader to
inspect it.

To write elsewhere or select a narrower trace, change the simulation options.
The output extension is sufficient for automatic format selection, or
`--trace-format` can make the choice explicit:

```sh
fsim simulate --trace /tmp/three-language.fst --trace-format fst \
  --trace-compression deterministic --trace-filter 'three_language_tb.*'
```

## 5. Instrument an interactive debug run

Start the debugger with the default snapshot:

```sh
fsim debug
```

At the `(fsim)` prompt, these commands inspect the HDL-owned SystemC and VHDL
siblings, select two live trace signals, and stop when the result becomes `1`:

```text
scopes
scope u_bridge
scopes
signals
scope ..
scope u_vhdl
signals
scope ..
break signal observed == 1
trace clear
trace add stimulus
trace add observed
continue
show observed
where
continue
quit
```

The expected stop is at time 1, delta 2. The complete sequence is checked in
as `debug-session.sh`, so it can also be replayed non-interactively:

```sh
sh debug-session.sh
```

Set `FSIM_BIN` when using a different executable, for example:

```sh
FSIM_BIN=../../build/release/fsim \
  sh debug-session.sh
```

Use `break source PATH:LINE` for an HDL statement, `break time 2ns` for a
scheduler stop, `step statement|process|delta|time` for controlled progress,
and `deposit`, `force`, or `release` to experiment with boundary values. Run
`help` in the debugger for the complete command summary.

## 6. Modify the example

A useful first experiment is to remove either inversion. The SV self-check
will then fail, while the VCD shows which side of the boundary stopped matching.
Other safe extensions are:

- widen the ports consistently in all three sources;
- add another `sc_signal` and `SC_METHOD` in `MixedBridge`;
- add a VHDL generic and supply it from the owning HDL hierarchy; or
- add a SystemC factory parameter and pass it from the SV placeholder.

Keep library names and search scope explicit when adding potentially ambiguous
cross-language targets. If a width, encoding, direction, or construction actual
is incompatible, elaboration rejects it instead of guessing a conversion.

Generated compiler state stays under `.fsim`; `three_language.vcd` is the
selected waveform output. The old `fsim.toml` and `fsim-fst.toml` files are
historical fixtures and are not loaded by current commands.
