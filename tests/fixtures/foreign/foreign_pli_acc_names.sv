// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 attributes (IEEE 1364-2001 23.10-23.12): specparams and parameters
// named as attributes of nets, and an intermodule path between ports.
`timescale 1ns / 1ns
module leaf(input a, output y);
  assign y = a;
endmodule

module top;
  wire w1, w2, w3;
  specparam Cap$w1 = 2.5;
  specparam Cap$ = 1.0;
  parameter Kind$w2 = 11;
  parameter Name$ = "gen";
  leaf u1(.a(w1), .y(w2));
  leaf u2(.a(w2), .y(w3));
  assign w1 = 1'b0;
  initial #1 $pli_acc_names;
endmodule
