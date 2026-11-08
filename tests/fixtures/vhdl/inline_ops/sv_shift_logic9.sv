// SPDX-License-Identifier: Apache-2.0
//
// Shifts of a net that a VHDL std_logic_vector drives (sv_shift_logic9_vhdl.vhd)
// in a SystemVerilog member, which keeps running in compiled code although
// the value holds 'U' (no deoptimization to the operation tier). The source
// is "UX01ZWLH", which the VHDL-to-SystemVerilog value mapping (IEEE 1800-2023
// 6.6 and the std_logic table of IEEE 1076-2019) reads as xx01zx01: 'U', 'X',
// 'W' -> x, '0', 'L' -> 0, '1', 'H' -> 1, 'Z' -> z. The shift operators are
// four-state operations (IEEE 1800-2023 11.4.10): the vacated bits are 0 and
// a shift amount with x or z gives all x.
//   l1 = i << 1   = x01zx01 then 0           = x01zx010
//   r2 = i >> 2   = 00 then xx01zx            = 00xx01zx
//   a1 = $signed(i) >>> 1: the sign bit is x, so the vacated bit is x
//                 = x then xx01zx0            = xxx01zx0
//   v3 = i << n, n = 3 from a variable        = bits 4..0 of i then 000
//                                              = 1zx01000
//   vx = i << n with n = 4'bx                 = xxxxxxxx
module svm(input wire [7:0] i);
  wire [7:0] l1 = i << 1;
  wire [7:0] r2 = i >> 2;
  wire signed [7:0] si = i;
  wire [7:0] a1 = si >>> 1;
  reg [3:0] n;
  reg [7:0] v3, vx;
  initial begin
    #1;
    n = 4'd3;
    v3 = i << n;
    n = 4'bx;
    vx = i << n;
    $display("inl shl l1=%b", l1);
    $display("inl shl r2=%b", r2);
    $display("inl shl a1=%b", a1);
    $display("inl shl v3=%b", v3);
    $display("inl shl vx=%b", vx);
  end
endmodule
