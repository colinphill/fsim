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

## 2. Compile and run in a workspace

Run from the directory containing `wide_literal.v`:

```sh
fsim check --lang verilog --standard 2005 wide_literal.v
fsim compile --lang verilog --standard 2005 --library work wide_literal.v
fsim elaborate work.wide_literal
fsim simulate --engine compiled --duration 2ns --max-deltas 1000 \
  --trace wide-literal.vcd
```

The installed `fsim-sv` alias accepts the same commands. Run simulation again
to exercise the warm native cache under `.fsim/cache`. The `WIDE=` transcript
and VCD value must remain identical. Use `fsim debug` for the source-aware
debugger; inspection retains the complete value.

## 3. Keep a source-independent snapshot

Elaborate a named snapshot from the library metadata:

```sh
fsim elaborate root=work.wide_literal --snapshot portable
fsim simulate --snapshot portable --engine interpreter \
  --trace portable-interpreter.vcd
fsim simulate --snapshot portable --engine compiled \
  --trace portable-compiled.vcd
```

Hide the producer source before repeating simulation. The snapshot under
`.fsim/snapshots/portable` retains the compiled state and remains usable after
library updates or deletion. For relocation, move the complete workspace
state rather than selecting individual artifact payloads. fsim manages all
artifact filenames and validates checksums and compatible identities.

## 4. Add timing or VPI observation

Specify blocks may add parameterized module paths, pulse policy, notifiers, and
the twelve standard timing-check families. Their normalized occurrence
identities survive the same artifact and cache flow. SDF 2.1, 3.0 and 4.0
annotations resolve those occurrences and apply Verilog/SystemVerilog path,
interconnect, port, device and timing-check backannotation with deterministic
precedence, delay selection, diagnostics and portable persistence. See
[SDF support](sdf.md) for the exact command, Tcl and artifact boundary.

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
2. verify `--lang verilog --standard 2005`;
3. compare interpreter, compiled cold/warm, debugger, and VCD values;
4. preserve the complete workspace snapshot during relocation; and
5. treat addressability, storage, work, scalar-ABI, and VPI-format failures as
   physical representation boundaries, not arbitrary literal legality limits.

See [Verilog-2005 support](verilog-2005.md) for the exact language boundary and
the [closure audit](verilog-2005-closure-audit.md) for governed evidence.
