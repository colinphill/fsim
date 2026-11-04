// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 parameter value change flags and delayed string puts (IEEE
// 1364-2001 25.9, 25.22, 25.33, 25.52, 25.55, 25.56, 25.59). $pli_tf_pvc
// watches a and b; $pli_tf_put writes o and p after delays of 2 to 15.
`timescale 1ns / 1ns
module top;
  reg [7:0] a;
  reg [7:0] b;
  reg [7:0] o;
  reg [7:0] p;
  always @(o) if ($time > 0) $display("#c design o=%h at %0d", o, $time);
  always @(p) if ($time > 0) $display("#c design p=%h at %0d", p, $time);
  initial begin
    a = 8'd1;
    b = 8'd2;
    o = 8'd0;
    p = 8'd0;
    $pli_tf_pvc(a, b);
    $pli_tf_put(o, p);
    #10 a = 8'd3;
    #10 b = 8'd4;
    #10 a = 8'd5;
    #10 b = 8'd7;
    #10 a = 8'd9;
    #10 $display("#c design done");
    $finish;
  end
endmodule
