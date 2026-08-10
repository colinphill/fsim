<!-- SPDX-License-Identifier: Apache-2.0 -->
# Running VHDL-2008 with embedded PSL

This tutorial uses ordinary VHDL source and public fsim commands. It does not
depend on the internal conformance runner.

## 1. Write a clocked PSL property

Save this as `psl_counter.vhd`:

```vhdl
library ieee;
use ieee.std_logic_1164.all;

entity psl_counter is end entity;

architecture rtl of psl_counter is
  signal clk : std_logic := '0';
  signal request : std_logic := '1';
  signal acknowledge : std_logic := '0';
  -- psl default clock is rising_edge(clk);
  -- psl property response is request = '1' |-> next acknowledge = '1';
begin
  -- psl CHECK_RESPONSE: assert response;

  clock_driver : process
  begin
    wait for 1 ns;
    clk <= not clk;
  end process;

  stimulus : process
  begin
    wait for 1 ns;
    acknowledge <= '1';
    wait;
  end process;
end architecture;
```

Unknown clock values do not create a sample. Assert/assume failures are
reported, restrict failures use warning routing, and cover misses affect
coverage only.

## 2. Create a project

Save this beside the source as `fsim.toml`:

```toml
# SPDX-License-Identifier: Apache-2.0
schema = 2

[project]
name = "psl-counter"
top = "vhdl:work.psl_counter(rtl)"
time_resolution = "1ns"

[[source_set]]
language = "vhdl"
standard = "2008"
library = "work"
files = ["psl_counter.vhd"]

[build]
optimization = "O2"
jobs = 8
cache_path = ".fsim-cache"

[run]
duration = "10ns"
max_deltas = 1000
trace_file = "psl-counter.vcd"
```

Check and run it with either the primary executable or installed VHDL alias:

```sh
fsim check -p fsim.toml
fsim run -p fsim.toml
fsim-vhdl run -p fsim.toml
```

Run a second time to exercise the warm native cache. The attempt outcomes and
VCD must remain identical.

## 3. Inspect VHDL and PSL state

Start the debugger:

```sh
fsim debug -p fsim.toml
```

The `vhdl summary`, `vhdl scopes`, `vhdl objects`, `vhdl processes`, `vhdl
psl`, and `vhdl all` commands inspect the same bounded occurrence-owned state
used by callbacks and VCD. Signal breakpoints can stop at a clock change; clear
the breakpoint and continue to retain the same final PSL results.

## 4. Produce source-independent artifacts

The portable flow separates compile, elaborate, and simulate:

```sh
fsim compile --lang vhdl --standard 2008 --library work \
  --output psl-counter.fsimobj psl_counter.vhd
fsim elaborate --object psl-counter.fsimobj \
  --top root=vhdl:work.psl_counter(rtl) \
  --output psl-counter.fsimdesign
fsim simulate --design psl-counter.fsimdesign --engine compiled \
  --cache .fsim-cache/portable --trace portable.vcd
```

Move the complete `.fsimdesign` directory and hide the producer source before
the final command to test relocation. Do not mix individual payload files or
reuse a cache/checkpoint from an incompatible artifact identity; validation
rejects those combinations before simulation state is published.

## 5. Diagnose failures

Treat the run as successful only when analysis and elaboration have no error,
simulation reaches the expected time or completion, and the intended PSL
attempt/coverage result is present. A zero process exit code is not a substitute
for those semantic checks.

For a failure:

1. inspect the complete `FSIM-VHDL-PSL-*` diagnostic and source location;
2. check that every property has a compatible explicit or inherited clock;
3. distinguish failure from vacuity or abort in `vhdl psl` output;
4. compare cold/warm VCD and debugger state;
5. verify that relocated artifacts moved as complete directories; and
6. treat resource-limit errors as physical/work boundaries, not language
   syntax errors.

See [VHDL/PSL support](vhdl-psl.md) for the exact language boundary and
[closure audit](vhdl-psl-closure-audit.md) for the governed evidence.
