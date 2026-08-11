<!-- SPDX-License-Identifier: Apache-2.0 -->
# Running Verilog-2005 with exact wide literals

This tutorial uses ordinary Verilog source and public fsim commands. It does
not depend on the internal closure runner.

## 1. Write a wide four-state design

Save this as `wide_literal.v`:

```verilog
`timescale 1ns/1ps

module wide_literal;
  reg signed [136:0] value;

  initial begin
    value = 137'h1_0000_0000_0000_0000_0000_0000_0000_0000_xz;
    $display("WIDE=%h", value);
    #1;
    $finish;
  end
endmodule
```

The literal is wider than a host word and contains both unknown and
high-impedance digits. Fsim must retain its exact 137-bit width, signedness, and
four-state planes; splitting it into 64-bit chunks is neither required nor
recommended.

## 2. Create and run a project

Save this beside the source as `fsim.toml`:

```toml
# SPDX-License-Identifier: Apache-2.0
schema = 2

[project]
name = "wide-verilog"
top = "verilog:work.wide_literal"
time_resolution = "1ps"

[[source_set]]
language = "verilog"
standard = "2005"
library = "work"
files = ["wide_literal.v"]

[build]
optimization = "O2"
jobs = 8
cache_path = ".fsim-cache"

[run]
duration = "2ns"
max_deltas = 1000
trace_file = "wide-literal.vcd"
```

Check and run it with the primary executable or installed Verilog alias:

```sh
fsim check -p fsim.toml
fsim run -p fsim.toml
fsim-sv run -p fsim.toml
```

Run it a second time to exercise the warm native cache. The `WIDE=` transcript
and VCD value must remain identical. Use `fsim debug -p fsim.toml` for the
source-aware debugger; inspection must show the same complete value rather than
a low-word projection.

## 3. Produce source-independent artifacts

The public phase interface can compile, elaborate, and simulate the same design
without a project manifest:

```sh
fsim compile --lang verilog --standard 2005 --library work \
  --output wide-literal.fsimobj wide_literal.v
fsim elaborate --object wide-literal.fsimobj \
  --top root=verilog:work.wide_literal \
  --output wide-literal.fsimdesign
fsim simulate --design wide-literal.fsimdesign --engine interpreter \
  --trace portable-interpreter.vcd
fsim simulate --design wide-literal.fsimdesign --engine compiled \
  --cache .fsim-cache/portable --trace portable-compiled.vcd
```

Move the complete `.fsimdesign` directory and hide the producer source before a
final simulation to exercise relocation. Move artifact directories as units;
mixing individual payload files, stale checkpoints, or incompatible cache state
must reject before simulation state is published.

## 4. Add timing or VPI observation

Specify blocks may add parameterized module paths, pulse policy, notifiers, and
the twelve standard timing-check families. Their normalized occurrence
identities survive the same artifact and cache flow; SDF annotation remains a
separate Batch 170 feature.

VPI clients should request vector formats for values wider than 64 bits. The
integer format is intentionally scalar, but the vector descriptor has no
one-megabit admission cap and retains the complete host-addressable value. See
[SystemVerilog VPI support](systemverilog-vpi.md) for the plug-in ABI.

## 5. Diagnose failures

Treat the run as successful only when analysis and elaboration contain no error,
simulation advances to the expected time or reaches `$finish`, the complete
`WIDE=` value is present, and interpreter/compiled/cache/VCD observations agree.
A zero process exit code is not a substitute for those semantic checks.

For a failure:

1. retain the complete diagnostic and source location;
2. verify `language = "verilog"` and `standard = "2005"`;
3. compare interpreter, compiled cold/warm, debugger, and VCD values;
4. move portable artifact directories as complete units; and
5. treat addressability, storage, work, scalar-ABI, and VPI-format failures as
   physical representation boundaries, not arbitrary literal legality limits.

See [Verilog-2005 support](verilog-2005.md) for the exact language boundary and
the [closure audit](verilog-2005-closure-audit.md) for governed evidence.
