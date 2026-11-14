// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 attributes (IEEE 1364-2001 23.10-23.12): specparams and parameters
// named as attributes of nets, and an intermodule path between ports and
// between bits of vector ports (23.50).
`timescale 1ns / 1ns
module leaf(input a, output y);
  assign y = a;
endmodule

module vleaf(input [3:0] a, output [3:0] y);
  assign y = a;
endmodule

module pad(inout io);
  always @(io) if ($time >= 5) $display("#c pad %m io=%b t=%0d", io, $time);
endmodule

module top;
  wire w1, w2, w3;
  specparam Cap$w1 = 2.5;
  specparam Cap$ = 1.0;
  parameter Kind$w2 = 11;
  parameter Name$ = "gen";
  leaf u1(.a(w1), .y(w2));
  leaf u2(.a(w2), .y(w3));
  wire [3:0] vb, vc;
  vleaf v1(.a(4'h5), .y(vb));
  vleaf v2(.a(vb), .y(vc));
  assign w1 = 1'b0;
  // Two inout ports on one net: the delay of the path to the second is the
  // module input port delay of that inout port (IEEE 1364-2005 16.2.4).
  wire w5;
  reg d5 = 1'b0;
  assign w5 = d5;
  pad p1(.io(w5));
  pad p2(.io(w5));
  initial #10 d5 = 1'b1;
  initial #1 $pli_acc_names;
endmodule
