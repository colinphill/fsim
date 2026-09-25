<!-- SPDX-License-Identifier: Apache-2.0 -->
# Vertical-slice example

This compact VHDL-2008/SystemVerilog-2017 example demonstrates mixed-language
hierarchy, shared scheduling, multiple roots, tracing, and source-aware
debugging in a managed workspace.

- `tb.sv` drives the clock/reset, instantiates the VHDL counter and SV child,
  uses integer delays, and calls `$finish`.
- `counter.vhd` is a VHDL counter using a recognized `rising_edge` guard.
- `sv_child.sv` is a combinational SystemVerilog inverter.

Run from this directory, with fsim on `PATH`:

```sh
fsim check --lang vhdl --standard 2008 counter.vhd
fsim check --lang systemverilog --standard 2017 tb.sv sv_child.sv
fsim compile --library work counter.vhd
fsim compile --library work tb.sv sv_child.sv
fsim elaborate work.tb
fsim simulate --engine compiled --trace vertical.vcd
fsim debug
```

The elaborator resolves `counter` and `sv_child` uniquely from `work` and
constructs `tb.u_counter` and `tb.u_child`. Connected child ports alias the
top signals, so committed changes propagate through the shared delta
scheduler. Library metadata and the default snapshot are managed in `.fsim`.

At the bounded final tick, the committed top signals are:

```text
counter_q = 00000001
child_y   = 11111110
```

The VCD therefore shows counter value `1` and child value `FE`. Each boundary
signal has one driver.

To create two independent root instances in one snapshot:

```sh
fsim elaborate left=work.tb right=work.tb --snapshot two-roots
fsim simulate --snapshot two-roots --trace two-roots.vcd
```

Both copies resolve before either hierarchy is constructed. Their objects
appear under `left.*` and `right.*`; the first `$finish` remains terminal for
the shared simulation. Recompiling a source and re-elaborating replaces the
selected library definitions and snapshot. The `default` snapshot remains
available when `two-roots` is replaced.

The example uses equal-width descending vectors and whole-signal connections.
See the [workspace phase tutorial](../non_project_phases/README.md) and
[three-language tutorial](../three_language_hierarchy/README.md) for mapped
libraries and SystemC. The historical `fsim.toml` fixture is not loaded by
current commands.
