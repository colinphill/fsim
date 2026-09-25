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

## 2. Compile and elaborate a workspace snapshot

Run from the directory containing `psl_counter.vhd`:

```sh
fsim check --lang vhdl --standard 2008 psl_counter.vhd
fsim compile --lang vhdl --standard 2008 --library work psl_counter.vhd
fsim elaborate 'work.psl_counter(rtl)'
fsim simulate --duration 10ns --max-deltas 1000 --trace psl-counter.vcd
```

The installed `fsim-vhdl` alias accepts the same commands. Parentheses are
quoted for the shell. The top's language is inferred from library metadata.
Run simulation again to exercise the warm cache; PSL attempt outcomes and
VCD must remain identical.

## 3. Inspect VHDL and PSL state

Start the debugger:

```sh
fsim debug
```

The `vhdl summary`, `vhdl scopes`, `vhdl objects`, `vhdl processes`, `vhdl
psl`, and `vhdl all` commands inspect the same bounded occurrence-owned state
used by callbacks and VCD. Signal breakpoints can stop at a clock change; clear
the breakpoint and continue to retain the same final PSL results.

## 4. Keep a source-independent snapshot

```sh
fsim elaborate 'root=work.psl_counter(rtl)' --snapshot portable
fsim simulate --snapshot portable --engine compiled --duration 10ns \
  --trace portable.vcd
```

Hide the producer source before repeating simulation. The snapshot under
`.fsim/snapshots/portable` remains usable after source removal or library
updates. Move complete workspace state for relocation; fsim manages artifact
filenames and rejects incompatible snapshots or checkpoints before publishing
simulation state.

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
5. verify that relocation preserved complete workspace state; and
6. treat resource-limit errors as physical/work boundaries, not language
   syntax errors.

See [VHDL/PSL support](vhdl-psl.md) for the exact language boundary and
[closure audit](vhdl-psl-closure-audit.md) for the governed evidence.
