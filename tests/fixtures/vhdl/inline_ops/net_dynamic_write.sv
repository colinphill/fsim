// SPDX-License-Identifier: Apache-2.0
//
// Dynamic-index and dynamic part-select writes of a SystemVerilog variable
// output port (IEEE 1800-2023 11.5.1 and 11.5.1 for the indexed part select
// of an lvalue; 23.3.3 for the port) whose net a VHDL process also drives
// (net_dynamic_write_vhdl.vhd drives 'Z' on the same std_logic_vector), so
// that the net is a resolved one with two sources (IEEE 1076-2019 14.7.3.4
// for the resolution; a 'Z' source is overridden by any other value).
//   q = 16'h0000          bits 15..0 = 0000000000000000
//   q[5] = 1              bit 5
//   q[9] = 1              bit 9
//   q[9 +: 3] = 3'b101    bits 11..9 = 101: bit 11 and bit 9 set, bit 10 clear
// so after the writes q = 0000 1010 0010 0000, which the VHDL process reports
// at 1 ns.
module svm(output logic [15:0] q);
  integer i;
  initial begin
    q = 16'h0000;
    i = 5;
    q[i] = 1'b1;
    i = 9;
    q[i] = 1'b1;
    q[i +: 3] = 3'b101;
  end
endmodule
