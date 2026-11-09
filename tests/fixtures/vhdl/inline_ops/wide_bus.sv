// SPDX-License-Identifier: Apache-2.0
//
// A wide std_logic_vector inout port that the VHDL leaf (wide_bus_vhdl.vhd)
// drives from a process while this module drives the same SystemVerilog net
// (its upper half through a continuous assignment that is high impedance
// when en is 0, bits 2 and W-1 always): the VHDL process compiles inline in the v4
// kernel at every width W (the elaboration generic), the unrolled chunk
// stores up to 8 chunks and the word loops above (65, 512, 513, 1400). The
// nine-value constants 'H', 'L', 'Z' and a signal of all nine kinds are
// assigned to a resolved net. Both languages print the net at 0.5 ns + k ns
// (this module) and 0.6 ns + k ns (the leaf), k = 0 to 5.
// IEEE 1800-2023 6.5 and 10.3: the net resolves its drivers; 21.2.1.3: %h
// prints x, z, X or Z for a group of four bits that holds unknown or
// high-impedance bits (bit W-1 is always driven so that no group the
// display pads is high impedance). IEEE 1076-2019 14.7.3.2: the net's value
// is the std_logic resolution of its sources; 'H' and 'L' are weak and lose
// to a strong source, an 'L' with an 'H' is 'W'.
// The sources of bit p of the net (V: the leaf's, S: this module's):
//   V by mode: 0 'H' | 1 data(p) = "10XZLHW-"(p mod 8) | 2 'L' | 3 'Z'
//   S in bits W-W/2 to W-2, q = p - (W - W/2): when en "01xz"(q mod 4), else
//   'z'; bits 2 and W-1 are always en.
//   t=0.5 mode 0 en 0 | t=1.5 mode 1 en 1 (en is 1 from 1.25) |
//   t=2.5 mode 2 en 1 | t=3.5 mode 3 en 0 (from 3.25) |
//   t=4.5 and 5.5 mode 0 (from 4) en 1 (from 4.25)
// A bit's value is the table of IEEE 1164 for V and S; this module reads
// 'H' as 1, 'L' as 0, and 'W', '-' and 'U' as x (IEEE 1800-2023 and the
// cross-language mapping); the leaf prints the resolved port with to_string and
// reads the SystemVerilog net as '0', '1', 'X' or 'Z' (docs/cross-language-
// semantics.md, Logic mapping: 'H' reads '1', 'L' '0', 'W' and '-' 'X').
module top;
  timeunit 1ns;
  timeprecision 10ps;
  parameter int W = 65;
  wire [W-1:0] bus_io;
  logic en = 1'b0;
  logic [W/2-2:0] value;
  assign bus_io[W-2:W-W/2] = en ? value : {(W/2-1){1'bz}};
  assign bus_io[2] = en;
  assign bus_io[W-1] = en;
  wide_bus_leaf #(.W(W)) u(.bus_io(bus_io));
  initial begin
    for (int q = 0; q < W/2 - 1; ++q) begin
      case (q % 4)
        0: value[q] = 1'b0;
        1: value[q] = 1'b1;
        2: value[q] = 1'bx;
        default: value[q] = 1'bz;
      endcase
    end
    #1.25 en = 1'b1;
    #2 en = 1'b0;
    #1 en = 1'b1;
  end
  initial begin
    #0.5;
    for (int step = 0; step < 6; ++step) begin
      $display("inl sv %0d bus=%h", step, bus_io);
      #1;
    end
  end
endmodule
