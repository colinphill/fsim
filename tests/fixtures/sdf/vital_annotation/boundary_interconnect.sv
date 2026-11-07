// SPDX-License-Identifier: Apache-2.0
// The INTERCONNECT of the SDF cell of a SystemVerilog module whose
// destination is the input port of the VHDL instance u_v (IEEE 1076.4-2000
// 5.2.7.1.4): it sets tipd_a of u_v.
`timescale 1ns/1ps
module bi_top(input logic a, output logic z);
  vbuf u_v(.a(a), .y(z));
endmodule

module bi_bench;
  logic a, z;
  bi_top dut(.a(a), .z(z));
  initial begin
    a = 0;
    #10 a = 1;
    #20 a = 0;
  end
  always @(z) $display("TRACE z=%b @%0d", z, $time);
endmodule
