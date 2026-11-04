<!-- SPDX-License-Identifier: Apache-2.0 -->
# cocotb with fsim

fsim runs [cocotb](https://www.cocotb.org) testbenches through its standard
VPI (SystemVerilog and Verilog) and VHPI (VHDL) interfaces. The integration
ships with fsim. There are two ways to use it: include fsim's simulator
makefile from a cocotb test Makefile, or call `get_runner("fsim")` from
Python. cocotb itself is not changed.

Supported cocotb versions: the 2.1.x releases and the 2.2 development line
(master). The integration finds the version at run time.

## Setup

Install cocotb in a Python environment (`pip install cocotb`), and put both
`fsim` and that environment's `cocotb-config` on `PATH`. Then:

| Command | Prints |
|---|---|
| `fsim cocotb makefile` | the path of the installed `Makefile.fsim` |
| `fsim cocotb pythonpath` | the directory that holds the `fsim_cocotb` Python package |
| `fsim cocotb libs` | the cocotb GPI libraries fsim uses (`vpi dsim`, `vhpi nvc`) |

fsim loads cocotb's own GPI libraries for DSim (VPI) and NVC (VHPI). Both
follow the LRMs without simulator-specific workarounds.

## Makefile flow

Set the usual cocotb variables, then include fsim's makefile in place of
cocotb's `Makefile.sim`:

```make
TOPLEVEL_LANG = verilog
VERILOG_SOURCES = $(PWD)/dff.sv
COCOTB_TOPLEVEL = dff
COCOTB_TEST_MODULES = test_dff
include $(shell fsim cocotb makefile)
```

`make` compiles the sources and elaborates the toplevel. Then
`fsim simulate` runs with the cocotb library loaded, and cocotb's
`results.xml` decides the result.

The makefile handles these variables:
- **Sources:** `VERILOG_SOURCES`, `VHDL_SOURCES`, `VHDL_SOURCES_<lib>` with
  `VHDL_LIB_ORDER`, `VERILOG_INCLUDE_DIRS`, and `TOPLEVEL_LIBRARY` (default
  `work`).
- **Tool arguments:** `COMPILE_ARGS`, `ELAB_ARGS` (for example `--generic
  WIDTH=6`), `SIM_ARGS`, `EXTRA_ARGS`, and `COCOTB_PLUSARGS`.
- **Time:** `COCOTB_HDL_TIMEUNIT` and `COCOTB_HDL_TIMEPRECISION` become
  `compile --timescale` for Verilog files that have no `` `timescale ``.
  When the design has VHDL sources, `COCOTB_HDL_TIMEPRECISION` also becomes
  `elaborate --time-resolution`, as cocotb's Questa flow does with `-t`.
  Without it, a design with VHDL units resolves time in femtoseconds.
- **Waves:** `WAVES=1` writes `<toplevel>.fst`.
- **Targets:** `make debug` runs the simulation under gdb, and `make clean`
  removes `.fsim`.

Each `fsim compile` takes one source language, inferred from the file
extension. The makefile compiles each library's VHDL first, then the
toplevel library's VHDL, its Verilog (`.v`) files and its SystemVerilog
files. The runner uses the same order. A
VHDL toplevel is named `vhdl:<library>.<toplevel>`, so a Verilog unit with
the same name cannot be picked by mistake.

The makefile also works as cocotb's `SIM=fsim` simulator makefile. For that,
copy it to `cocotb_tools/makefiles/simulators/Makefile.fsim` in the Python
environment. cocotb does not ship it there.

## Python runner flow

```python
import fsim_cocotb  # registers "fsim" with cocotb_tools.runner
from cocotb_tools.runner import get_runner

runner = get_runner("fsim")
runner.build(hdl_toplevel="counter", build_dir="sim_build",
             vhdl_sources=["counter.vhd"], timescale=("1ns", "1ps"))
runner.test(hdl_toplevel="counter", hdl_toplevel_lang="vhdl",
            test_module="test_counter", build_dir="sim_build",
            test_dir="sim_build", parameters={"WIDTH": 6})
```

Put `fsim cocotb pythonpath` on `PYTHONPATH`. `test_dir` must be
`build_dir`, because tests run in the workspace that `build()` compiled into.
`parameters` become `elaborate --generic NAME=VALUE`. With VHDL sources, the
precision of `timescale` becomes `elaborate --time-resolution`.

## Workspace

fsim's workspace is the directory that holds `.fsim`. Relative paths in
`$readmemh`, `$fopen` and similar calls resolve against it. The Makefile flow
uses the test directory, where `make` runs. The runner flow uses
`build_dir`.

## Behaviour

- **Writes:** with zero delay, `vpiInertialDelay` and `vhpiDepositPropagate`
  writes follow the LRMs. fsim therefore defaults
  `COCOTB_TRUST_INERTIAL_WRITES` to 1 in both flows, as cocotb does for nvc,
  GHDL and Verilator. A value set in the environment or on the `make` command
  line still wins.
- **Writes to driven variables:** a value written to a variable that a
  combinational block assigns (`always @(a) y = a;`) lasts until something
  that block is sensitive to changes, also when other logic fused with the
  block runs (IEEE 1800-2023 38.34). The block keeps running as native code.
- **Mixed-language designs:** both interfaces are served, whichever one the
  toplevel uses. cocotb's VPI library loads its VHPI layer, or the other way
  round. Each interface answers calls while the other's routines and
  callbacks run, so a test reaches signals in both languages, for example
  `dut.u_vhdl_child.internal_signal` under a SystemVerilog top.
- **VHDL instances in VPI:** IEEE 1800-2023 has no object type for an
  instance of a VHDL entity. VPI gives it fsim's own type,
  `fsimVpiForeignInstance` (1000), so cocotb's VPI layer hands it to its
  VHPI layer. It is one of its parent's `vpiInternalScope` objects and
  resolves by name, but `vpi_iterate(vpiModule, ...)` leaves it out: a
  plug-in that walks the hierarchy by modules alone stops at VHDL instances.
  In `vpi_handle_by_name`, a path component that names a VHDL root or a
  label of a VHDL architecture matches in either case, as VHDL basic
  identifiers do (IEEE 1076-2019 15.4.2); cocotb's VHPI layer upper-cases
  the names it hands back to VPI. Names inside a SystemVerilog scope keep
  their case.
- **Mixed clocking order:** a change made by SystemVerilog or through VPI
  (a cocotb clock, say) wakes VHDL processes in the same Active region as
  the SystemVerilog processes it wakes, before that region's nonblocking
  updates. A SystemVerilog flop feeding a VHDL flop on one clock therefore
  passes the old value, as Vivado and the commercial simulators do. No
  standard orders zero-delay events across languages
  (`docs/cross-language-semantics.md`).
- **VHPI time:** a `vhpiTimeT` time (callback times, `vhpi_get_time`,
  `vhpi_get_next_time`, after-delay callback delays) counts units of the
  resolution limit, `vhpiResolutionLimitP` (IEEE 1076-2019 22.2.7). A TIME
  value read with `vhpi_get_value` stays in femtoseconds, the position
  number of the TIME value.
- **Ending the run:** `vpi_control(vpiFinish)` and `vhpi_control(vhpiFinish)`
  end the run as a finish, and `fsim simulate` exits 0.
- **Simulator identity:** the product name the interfaces report is `fsim`,
  and the version is the fsim version. cocotb's tests take their standard
  branches for fsim.
- **Timers from ReadOnly (VHDL):** cocotb's `ReadOnly()` is VHPI's
  `vhpiCbRepEndOfTimeStep`, and cocotb registers timers from it. IEEE
  1076-2019 21.3.6.8 makes that an error. Both flows therefore pass
  `simulate --vhpi-end-of-step-timers` (configuration key
  `simulate.vhpi_end_of_step_timers`), an fsim extension. With it, an
  after-delay or timeout callback registered or enabled at the end of a time
  step is accepted. It runs at its delay from the current time, and never in
  the time step that is ending. Without the option, fsim keeps the LRM rule.
- **Open ports under a SystemVerilog instance:** a SystemVerilog
  instantiation may leave any port of a VHDL entity unconnected (IEEE
  1800-2023 23.3.2), including an input without a default expression. That
  input keeps its implicit initial value, for example `'U'` for `std_logic`.
  A VHDL instantiation keeps the IEEE 1076-2019 6.5.6.3 rule
  (FSIM-ELAB-BIND-027).
- **Bridge threads:** while the simulation thread waits in a callback, as it
  does for cocotb's `@bridge` functions, another thread may query the
  design: time, values, properties, handle lookups and iteration run in that
  callback's context. Writes, callback registration and control routines
  fail on any thread but the simulation thread. The library must not query
  from another thread while the simulation thread runs.

## Limitations

- **Name case of VHDL declarations:** `vhpiCaseNameP` gives the spelling of
  a name's first declaration in its design unit. If two scopes of one unit
  spell the same name differently, both report the first spelling.
- **Generic overrides:** `--generic` accepts integer, boolean and string
  values, and STD_ULOGIC character literals. Overrides of real, time,
  character and array generics are not yet supported.
- **Simulator selection by name:** cocotb's `Makefile.sim` and its pytest
  plug-in (`--cocotb-simulator fsim`) know only the simulators cocotb lists.
  Use the include form or `import fsim_cocotb` instead.
