<!-- SPDX-License-Identifier: Apache-2.0 -->
# Vertical-slice example

This directory is the first executable mixed-language architecture gate, not a
claim of complete v1 language support.

- `tb.sv` is the SV executable top. It drives the clock/reset, instantiates a
  VHDL-bound counter and an SV child, uses integer delays, and calls `$finish`.
- `counter.vhd` is a VHDL-2008 clocked counter using a recognized
  `rising_edge` guard.
- `sv_child.sv` is a combinational SystemVerilog inverter driven by the
  counter output.
- `fsim.toml` preserves source order; both child names resolve uniquely in
  logical library `work` without binding entries.

The elaborator recursively creates `tb.u_counter` and `tb.u_child`. The
resolver maps `counter` to the VHDL entity and `sv_child` to the
SystemVerilog module before validating their interfaces. Connected child
ports alias the corresponding top signals, so committed changes propagate
through the common delta scheduler.

From the repository root:

```sh
build/dev/fsim check -p examples/vertical_slice/fsim.toml
build/dev/fsim run   -p examples/vertical_slice/fsim.toml
build/dev/fsim debug -p examples/vertical_slice/fsim.toml
```

The current `run` and `debug` commands execute this design with the reference
SimIR interpreter. The run writes `vertical_slice.vcd` in this directory and
resolves:

```text
tb.u_counter -> vhdl:work.counter(rtl)
tb.u_child   -> sv:work.sv_child
```

After reset and two rising clock edges, the final committed values are:

```text
tb.counter_q = 00000001
tb.child_y   = 11111110
```

The VCD therefore shows counter value `1` and child value `FE`. No resolver is
required because each boundary signal has one driver.

To exercise the v2 multiple-root selection contract without changing the
committed manifest, replace its single top on the command line. Every repeated
top needs a unique alias:

```sh
build/dev/fsim check -p examples/vertical_slice/fsim.toml \
  --top left=sv:work.tb --top right=sv:work.tb
```

Both copies are resolved before either hierarchy is constructed. A run would
place them in one scheduler and expose their objects beneath `left.*` and
`right.*`; the first `$finish` remains terminal for that shared simulation.

This example deliberately uses equal-width, descending packed vectors and
whole-signal connections. Parameters/generics, expression actuals, arbitrary
vector-direction conversion, multi-driver resolution, and SystemC factory
instances are outside this architecture gate.
