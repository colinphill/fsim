// SPDX-License-Identifier: Apache-2.0
// Drives mixed_top and prints every change of its output.
`timescale 1ns/1ps
module mixed_bench;
  logic a, z;
  mixed_top dut(.a(a), .z(z));
  initial begin
    a = 0;
    #10 a = 1;
    #20 a = 0;
  end
  always @(z) $display("TRACE z=%b @%0d", z, $time);
endmodule
