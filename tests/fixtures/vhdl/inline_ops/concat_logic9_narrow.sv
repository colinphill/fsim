// SPDX-License-Identifier: Apache-2.0
//
// Concatenations of a selection of a net that a VHDL std_logic_vector drives
// (concat_logic9_narrow_vhdl.vhd), with four-state results of at most 64
// bits stored to SystemVerilog nets and to a variable. The source is the
// nine values "UX01ZWLH-" repeated (element k from the left is the
// (k mod 9)th character), which the VHDL-to-SystemVerilog value mapping
// (IEEE 1800-2023 6.6, 28 and the std_logic table of IEEE 1076-2019) reads as
// 'U', 'X', 'W', '-' -> x, '0', 'L' -> 0, '1', 'H' -> 1 and 'Z' -> z. A
// four-state result holds those mapped values, not the nine value codes.
// i[7:0] is the elements 72..79 from the left, "UX01ZWLH", read as xx01zx01;
// i[15:8] is the elements 64..71, "X01ZWLH-", read as x01zx01x.
//   c0 = {i[7:0], 1'b1}      = xx01zx01 then 1            = xx01zx011
//   c1 = {1'b0, i[15:8]}     = 0 then x01zx01x            = 0x01zx01x
//   c2 = {i[3:0], i[7:4]}    = zx01 then xx01             = zx01xx01
//   c3 = {i[7:0], 56'b0}     = xx01zx01 then 56 zeros
//   r0 = {i[7:0], 1'b1} assigned in an initial block      = xx01zx011
module svm(input wire [79:0] i);
  wire [8:0] c0 = {i[7:0], 1'b1};
  wire [8:0] c1 = {1'b0, i[15:8]};
  wire [7:0] c2 = {i[3:0], i[7:4]};
  wire [63:0] c3 = {i[7:0], 56'b0};
  reg [8:0] r0;
  initial begin
    #1;
    r0 = {i[7:0], 1'b1};
    $display("inl ncat c0=%b", c0);
    $display("inl ncat c1=%b", c1);
    $display("inl ncat c2=%b", c2);
    $display("inl ncat c3=%b", c3);
    $display("inl ncat r0=%b", r0);
  end
endmodule
