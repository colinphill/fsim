# VHDL delayed-signal zero-wait probe

This reduced VHDL-2008 fixture preserves the clock transitions and the
`wait for 0 ns` sample from `falling_edge_app`. The falling edge is due on
`clk'delayed(1 ns)` at 2 ns. After the zero-time wait, Vivado/XSim observes
that delayed value as `'0'`; one nanosecond later, the signal assignment has
settled and `delayed_sample` is also `'0'`.

The external oracle was Vivado/XSim 2025.2.1. The reduced source used for that
run has SHA-256
`4a307533f0103d0090432b82bbefe60334df52c51adf1cf874b58cb90f5e852d`.
The full application fixture was not accepted by XSim because its unrelated
`'transaction`, `'last_event`, and `'last_active` destination types failed
compilation. This reduced probe verifies only the delayed-signal behavior; it
does not claim that the full application fixture compiled in Vivado.

To reproduce with Vivado, run the commands from a temporary working directory
so generated simulator files stay outside the source tree:

```sh
VIVADO_BIN=/path/to/Vivado/bin
FIXTURE=/path/to/fsim/tests/fixtures/vhdl/scheduling
"$VIVADO_BIN/xvhdl" -2008 -work work "$FIXTURE/falling_edge_delayed_probe.vhd"
"$VIVADO_BIN/xelab" -mt off -debug off falling_edge_delayed_probe -s falling_edge_delayed_probe_snapshot
"$VIVADO_BIN/xsim" falling_edge_delayed_probe_snapshot -tclbatch "$FIXTURE/probe.tcl"
```

The two `Note:` lines should match [expected.out](expected.out). XSim also
prints the report time and source location, which are intentionally omitted
from that value-only transcript.
