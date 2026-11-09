// SPDX-License-Identifier: Apache-2.0
//
// A VHDL process (wide_dyn_bus_vhdl.vhd) assigns an N-bit std_logic_vector
// to the slice bus_io(idx + N - 1 downto idx) of a W-bit inout port whose
// net this module drives too (bit 0, from en): the write through a signal
// index of a value wider than 64 bits (N = 65, 512, 513 and 1000) to a
// resolved net compiles in the v4 kernel at every width, with no helper.
// Both languages print the net at 0.5 ns + k ns (this module) and
// 0.6 ns + k ns (the leaf), k = 0 to 4.
// IEEE 1076-2019 10.5.2.1 and 14.7.3.2: a slice assignment with a
// nonstatic index drives the whole signal bus_io from the process, the
// elements outside the slice keep their driver values, which start at 'U'
// (the default value of the port) and are those the earlier slice
// assignments gave. IEEE 1164: the net's value is the resolution of the
// driver and of this module's source (bit 0: en, a strong 0 or 1; every
// other bit 'z'). This module reads 'U' as x, the leaf prints the net as
// '0', '1', 'X' or 'Z' (docs/cross-language-semantics.md, Logic mapping).
// The slice is at idx = 3 (from 0 ns), W - N (1 ns) and 0 (2 ns); the value
// is "10XZLHW-"((q + shift) mod 8) for bit q of the slice, shift 0 and from
// 3 ns shift 1; en is 1 from 2.25 ns.
module top;
  timeunit 1ns;
  timeprecision 10ps;
  parameter int W = 72;
  parameter int N = 65;
  wire [W-1:0] bus_io;
  logic en = 1'b0;
  assign bus_io[0] = en;
  wide_dyn_leaf #(.W(W), .N(N)) u(.bus_io(bus_io));
  initial begin
    #2.25 en = 1'b1;
  end
  initial begin
    #0.5;
    for (int step = 0; step < 5; ++step) begin
      $display("inl sv %0d bus=%h", step, bus_io);
      #1;
    end
  end
endmodule
