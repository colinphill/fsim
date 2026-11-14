// SPDX-License-Identifier: Apache-2.0
// Nets of user-defined nettypes across a save and a restart (IEEE 1800-2023
// 6.6.7, D.9): a packed net, an array of them, and an array of real nets.
// A save at the end of time zero holds their time-zero resolutions, which a
// restart does not repeat; a restart resolves again when a driver changes.
`timescale 1ns/1ns
module top;
  function automatic logic [7:0] rsum(input logic [7:0] d[]);
    rsum = 0;
    foreach (d[i]) rsum += d[i];
  endfunction
  function automatic real ravg(input real d[]);
    ravg = 0.0;
    foreach (d[i]) ravg += d[i];
    if (d.size() > 0) ravg = ravg / d.size();
  endfunction
  nettype logic [7:0] wsum with rsum;
  nettype real wavg with ravg;
  wsum w;
  wsum v [2];
  wavg r [2];
  logic [7:0] a = 1, b = 2;
  real p = 1.0, q = 2.0;
  assign w = a;
  assign w = b;
  assign v[0] = a;
  assign v[1] = b;
  assign v[1] = a;
  assign r[0] = p;
  assign r[0] = q;
  assign r[1] = q;
  always @(w) $display("t=%0t w=%0d", $time, w);
  initial begin
    #5 $display("t=%0t v=%0d %0d r=%f %f", $time, v[0], v[1], r[0], r[1]);
    #10 a = 10; q = 4.0;
    #10 $display("t=%0t v=%0d %0d r=%f %f", $time, v[0], v[1], r[0], r[1]);
    b = 20;
    #10 $display("t=%0t w=%0d v=%0d %0d", $time, w, v[0], v[1]);
  end
endmodule
