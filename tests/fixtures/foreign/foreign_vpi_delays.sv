// SPDX-License-Identifier: Apache-2.0
// Delays through the VPI (IEEE 1800-2023 38.10, 38.32): gates, a UDP
// instance, continuous assignments, a module path and a timing check.
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
  wire y_and, y_buf, y_udp, w_assign, w_path;

  and #(2, 3) g_and (y_and, a, b);
  bufif1 #(1:2:3, 4:5:6, 7:8:9) g_buf (y_buf, a, en);
  udp_inv #(2, 3) u_inv (y_udp, a);
  assign #(5, 6) w_assign = a;
  timed p (a, d, clk, w_path);

  initial begin
    #10 a = 1'b1;
    #10 a = 1'b0;
    // The plug-in changes the delays at 25 ns.
    #10 a = 1'b1;
    #30 $finish;
  end
endmodule
