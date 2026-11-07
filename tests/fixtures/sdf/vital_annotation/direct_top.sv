// SPDX-License-Identifier: Apache-2.0
// A SystemVerilog module that instantiates the VITAL model vital_core
// (examples/sdf_vital_mixed/vital_cell.vhd) directly. The VHDL port z is
// a std_logic signal that the SystemVerilog net z carries (IEEE 1800-2023
// 23.11, IEEE 1076-2019 6.5.6.3).
`timescale 1ns/1ps
module direct_top(input logic a, output logic z);
  vital_core u_vital(.a(a), .z(z));
endmodule

module direct_bench;
  logic a, z;
  direct_top dut(.a(a), .z(z));
  initial begin
    a = 0;
    #10 a = 1;
    #20 a = 0;
  end
  always @(z) $display("TRACE z=%b @%0d", z, $time);
endmodule
