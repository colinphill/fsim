<!-- SPDX-License-Identifier: Apache-2.0 -->
# Three-language hierarchy tutorial

This project is a runnable SystemVerilog, SystemC, and VHDL example. It uses
explicit bindings at both language boundaries and executes every language in
one recursively elaborated hierarchy:

```text
three_language_tb                         SystemVerilog testbench
└── u_bridge                              SystemC MixedBridge factory
    ├── invert_for_vhdl                   SystemC method
    ├── to_vhdl                           SystemC internal signal
    └── u_vhdl                            VHDL logic_stage(rtl)
```

The SV testbench drives `stimulus`. The SystemC method inverts it into the
internal `to_vhdl` signal, and the VHDL stage inverts it again. The
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

`build` recursively elaborates both explicit bindings, compiles the SystemC
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

## 3. Understand the bindings

The SystemVerilog source names `mixed_bridge_placeholder`, but the name does
not guess a foreign implementation. The first `[[binding]]` in `fsim.toml`
selects the registered SystemC factory explicitly:

```text
three_language_tb.u_bridge -> systemc:models.mixed_bridge
```

During factory construction, `MixedBridge` registers `u_vhdl` with
`fsim::systemc::hdl_instance` and connects its ports. The second binding then
selects the VHDL implementation at the complete child path:

```text
three_language_tb.u_bridge.u_vhdl -> vhdl:work.logic_stage(rtl)
```

Changing a source placeholder does not silently change either target; update
the matching full instance path in the manifest as well.

## 4. Record and inspect a VCD

The manifest enables tracing with:

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
three_language_tb.u_bridge.to_vhdl
three_language_tb.u_bridge.u_vhdl.value
three_language_tb.u_bridge.u_vhdl.result
```

The internal SystemC signal and the VHDL ports change in later deltas than the
SV stimulus, making the two boundary crossings visible. To write elsewhere or
select a narrower trace without editing the manifest, use a CLI override:

```sh
build/dev/fsim run -p examples/three_language_hierarchy/fsim.toml \
  -j 8 --trace /tmp/three-language.vcd
```

## 5. Instrument an interactive debug run

Start the debugger with the same project:

```sh
build/dev/fsim debug -p examples/three_language_hierarchy/fsim.toml -j 8
```

At the `(fsim)` prompt, these commands walk from the SV top through SystemC to
VHDL, select two live trace signals, and stop when the result becomes `1`:

```text
scopes
scope u_bridge
scopes
signals
scope u_vhdl
signals
scope ..
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
- add a VHDL generic and supply it with `u_vhdl.set_actual`; or
- add a SystemC factory parameter and pass it from the SV placeholder.

Keep cross-language targets explicit in `fsim.toml`. If a width, encoding,
direction, construction actual, or full instance path is incompatible, fsim
rejects the project during elaboration instead of guessing a conversion.

Generated files are local to this example: `.fsim-cache/` contains compiled
artifacts and `three_language.vcd` contains the waveform. Neither is source.
