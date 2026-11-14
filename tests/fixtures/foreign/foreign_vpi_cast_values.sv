// SPDX-License-Identifier: Apache-2.0
// The value of a cast operation (IEEE 1800-2023 6.24.1, 37.78 vpiCastOp):
// its operand converted to the casting type as an assignment would (sign
// extension of a signed operand, truncation, x and z to 0 in a two-state
// type, a real rounded to the nearest integer, 6.12.2).
module top;
  parameter int W = 6;
  logic [7:0] a = 8'b1010_x01z;
  logic signed [3:0] s = -4'sd3;
  real r = 2.5;
  real nr = -2.5;
  logic [31:0] y;
  real q;
  initial begin : main
    y = 4'(a);
    y = 12'(s);
    y = (W + 4)'(a);
    y = signed'(a);
    y = unsigned'(s);
    y = int'(a);
    y = byte'(s);
    y = int'(r);
    y = int'(nr);
    q = real'(s);
    q = shortreal'(r);
    y = integer'(a);
    y = logic'(a);
    y = bit'(a);
    y = const'(a);
  end
endmodule
