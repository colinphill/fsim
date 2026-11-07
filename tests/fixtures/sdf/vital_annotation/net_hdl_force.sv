// SPDX-License-Identifier: Apache-2.0
// A force and release statement of a net with per-source INTERCONNECT
// delays, made outside the load (IEEE 1800-2023 10.6.2): like a foreign
// write, it is a change no source made, so it reaches the load's delayed
// copy after the smallest of the groups' delays for its transition (the
// rule of IEEE 1076.4-2000 6.4.1 for a VITAL multiple source wire). After a
// release the load returns to the groups' resolved value.
//
// w has two sources, ua and ub, both off until 100 ns (w is z). The
// INTERCONNECT tables (01 10 0z z1 1z z0 ...) are ua 2 3 2 4 2 6 and
// ub 5 5 5 3 5 2. The probe p prints each change of the load from 10 ns:
//   20 ns  force w = 1: z->1, min(4, 3) = 3.         TRACE a=1 t=23
//   40 ns  release w (w is z): 1->z, min(2, 5) = 2.  TRACE a=z t=42
//   60 ns  force w = 0: z->0, min(6, 2) = 2.         TRACE a=0 t=62
//   80 ns  release w: 0->z, min(2, 5) = 2.           TRACE a=z t=82
//  100 ns  ua drives 1: its own z->1 delay, 4.       TRACE a=1 t=104
`timescale 1ns/1ps
module tdrv(input logic a, input logic en, output tri z);
  assign z = en ? a : 1'bz;
endmodule

module probe(input wire a);
  always @(a) if ($time >= 10) $display("TRACE a=%b t=%0d", a, $time);
endmodule

module top;
  logic ea = 0, da = 0, eb = 0, db = 0;
  wire w;
  tdrv ua(.a(da), .en(ea), .z(w));
  tdrv ub(.a(db), .en(eb), .z(w));
  probe p(.a(w));
  initial begin
    #20 force w = 1'b1;
    #20 release w;
    #20 force w = 1'b0;
    #20 release w;
    #20 begin da = 1; ea = 1; end
    #20 $finish;
  end
endmodule
