<!-- SPDX-License-Identifier: Apache-2.0 -->
# Three-language hierarchy tutorial

This project is a runnable SystemVerilog, SystemC, and VHDL example. It uses
automatic parent-plus-search-library resolution and executes every language
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
because fsim compiles `mixed_bridge.cpp` into a cached host plug-in while
building the project.

```sh
cmake --preset dev
cmake --build --preset dev --parallel 8 --target fsim
```

The remaining commands use `build/dev/fsim`. Substitute another configured
fsim executable if you built a different preset.

## 2. Check, build, and run

`check` parses and analyzes the three source files without executing them:

```sh
build/dev/fsim check -p examples/three_language_hierarchy/fsim.toml
```

`build` recursively resolves both boundaries, compiles the SystemC
plug-in, and prepares the selected execution engine. `-j 8` keeps the local
build at eight workers.

```sh
build/dev/fsim build -p examples/three_language_hierarchy/fsim.toml -j 8
build/dev/fsim run   -p examples/three_language_hierarchy/fsim.toml -j 8
```

The run should end with:

```text
SV observed 0 after the SystemC -> VHDL path
SV observed 1 after the SystemC -> VHDL path
PASS: SystemVerilog -> SystemC -> VHDL hierarchy
simulation stopped at tick 3, delta 0
```

The first build populates `examples/three_language_hierarchy/.fsim-cache`.
Repeating it reuses the checked analysis, SystemC plug-in, and eligible native
code. Remove only that example-local cache when you intentionally want to
observe a cold build.

## 3. Understand target resolution

The SystemVerilog source in `work` names both `mixed_bridge` and `LogicStage`.
They match the public factory alias exported by `SC_FSIM_EXPORT_AS` and the
VHDL entity in logical library `models`. The manifest makes that library
visible during unqualified elaboration:

```toml
[elaboration]
search_libraries = ["models"]
```

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

Repeated `--search-library` options replace the manifest list for one command.
An explicit full-path `[[binding]]` can still override either inferred target.
If a name is missing or ambiguous anywhere in the complete scope, elaboration
stops before comparing ports or construction actuals.

For an explicitly scripted build of the same three source files using
`.fsimobj`, `.fsimscobj`, `.fsimscplugin`, and `.fsimdesign`, follow the
[manifest-free phase tutorial](../non_project_phases/README.md#add-an-incrementally-compiled-systemc-library).

## 4. Record and inspect VCD or FST

The default manifest enables VCD tracing with:

```toml
[run]
trace_file = "three_language.vcd"
trace_filters = ["three_language_tb.*"]
```

After `run`, open
`examples/three_language_hierarchy/three_language.vcd` in a VCD viewer or
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

The sibling `fsim-fst.toml` manifest uses the same three-language sources and
selection but records deterministic FST:

```sh
build/dev/fsim run \
  -p examples/three_language_hierarchy/fsim-fst.toml -j 8
```

It writes `examples/three_language_hierarchy/three_language.fst`. The FST
contains the same canonical mixed-language hierarchy, aliases, values, and
ordered time changes as the VCD while using the deterministic portable
compression profile. Use an FST-capable viewer or fsim's bounded reader to
inspect it.

To write elsewhere or select a narrower trace without editing either manifest,
use CLI overrides. The output extension is sufficient for automatic format
selection, or `--trace-format` can make the choice explicit:

```sh
build/dev/fsim run -p examples/three_language_hierarchy/fsim.toml \
  -j 8 --trace /tmp/three-language.fst --trace-format fst \
  --trace-compression deterministic --trace-filter 'three_language_tb.*'
```

## 5. Instrument an interactive debug run

Start the debugger with the same project:

```sh
build/dev/fsim debug -p examples/three_language_hierarchy/fsim.toml -j 8
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
sh examples/three_language_hierarchy/debug-session.sh
```

Set `FSIM_BIN` when using a different executable, for example:

```sh
FSIM_BIN=build/release/fsim \
  sh examples/three_language_hierarchy/debug-session.sh
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

Keep cross-language targets explicit in `fsim.toml`. If a width, encoding,
direction, construction actual, or full instance path is incompatible, fsim
rejects the project during elaboration instead of guessing a conversion.

Generated files are local to this example: `.fsim-cache/` contains compiled
artifacts and `three_language.vcd` contains the waveform. Neither is source.
