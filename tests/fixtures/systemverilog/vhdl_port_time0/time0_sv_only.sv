// SPDX-License-Identifier: Apache-2.0
// The initializer is no event for SystemVerilog processes (IEEE 1800-2023 6.8).
module top;
  logic p = 1'b0;
  always @(p) $display("sv always @(p) p=%b at %0t", p, $time);
  initial @(p) $display("sv initial @(p) p=%b at %0t", p, $time);
  initial #0 $display("sv #0 p=%b", p);
  initial #1 $finish;
endmodule
