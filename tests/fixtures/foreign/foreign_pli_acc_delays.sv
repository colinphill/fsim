// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 delay and pulse routines (IEEE 1364-2001 22.8, Clause 23) over
// gates, a UDP instance, a module path and a timing check: $acc_delays_get
// reads them, $acc_delays_put changes them at 25 ns and the outputs show the
// new timing.
`timescale 1ns/1ps
primitive udp_inv (output y, input a);
  table
    0 : 1;
    1 : 0;
  endtable
endprimitive

// Module path terminals are ports of the module that declares the path
// (IEEE 1800-2023 30.3).
module timed (input a, input d, input clk, output w);
  assign w = a;

  specify
    (a => w) = (6, 7);
    $setup(d, posedge clk, 8);
  endspecify
endmodule

module top;
  reg a = 1'b0;
  reg b = 1'b1;
  reg en = 1'b1;
  reg d = 1'b0;
  reg clk = 1'b0;
  wire y_and, y_buf, y_udp, w_path;

  and #(2, 3) g_and (y_and, a, b);
  bufif1 #(1:2:3, 4:5:6, 7:8:9) g_buf (y_buf, a, en);
  udp_inv #(2, 3) u_inv (y_udp, a);
  timed p (a, d, clk, w_path);

  always @(y_and) if ($time >= 10) $display("#c change y_and %0d %b", $time, y_and);
  always @(y_udp) if ($time >= 10) $display("#c change y_udp %0d %b", $time, y_udp);
  always @(w_path) if ($time >= 10) $display("#c change w_path %0d %b", $time, w_path);

  initial begin
    #1 $acc_delays_get;
    #9 a = 1'b1;
    #10 a = 1'b0;
    #5 $acc_delays_put;
    #5 a = 1'b1;
    #30 $finish;
  end
endmodule
