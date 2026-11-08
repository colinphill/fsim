// SPDX-License-Identifier: Apache-2.0
//
// Concatenations of a net that a VHDL std_logic_vector drives (a nine value
// source, concat_logic9_vhdl.vhd) with SystemVerilog constants and with
// itself, which have a four-state result: 80 + 8, 8 + 80 and 80 + 80 bits,
// compiled inline by the v4 kernel (KOp::wide_concat), with no operation
// helper. The result is a Logic4 value, so each element of the source reads
// as the four-state value of the VHDL-to-SystemVerilog value mapping (the
// value table of Xilinx UG900 / IEEE 1076-2019 and 1800-2023 6.6 and 28
// for the net): 'U', 'X', 'W' and '-' read as x, '0' and 'L' as 0, '1' and
// 'H' as 1, 'Z' as z. The source "UX01ZWLH-" repeated reads as
// "xx01zx01x" repeated, E below (80 characters, the first of the 9th
// repeat cut at the 80th):
//   E = xx01zx01xxx01zx01xxx01zx01xxx01zx01xxx01zx01xxx01zx01xxx01zx01xxx01zx01xxx01zx01
// c1 = {i, 8'hff} is E then 11111111, c2 = {8'h5a, i} is 01011010 then E
// and c3 = {i, i} is E twice.
module svm(input wire [79:0] i);
  wire [87:0] c1 = {i, 8'hff};
  wire [87:0] c2 = {8'h5a, i};
  wire [159:0] c3 = {i, i};
  initial begin
    #1;
    $display("inl cat c1=%b", c1);
    $display("inl cat c2=%b", c2);
    $display("inl cat c3=%b", c3);
  end
endmodule
