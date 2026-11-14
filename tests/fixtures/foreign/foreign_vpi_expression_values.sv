// SPDX-License-Identifier: Apache-2.0
// Expression values and source forms seen through VPI at 1 (IEEE 1800-2023
// 37.59, 37.78): the array query functions over an object argument (20.7),
// operations over strings (6.16 Table 6-9, 11.4.11) and vpiDecompile
// (37.59 detail 2: one space between operands and operators, parentheses
// only where precedence needs them). From the LRM:
//   mem is logic [3:0] mem [2:5]: two dimensions, one unpacked; $left is
//   2, dimension 2 ([3:0]) has $right 0; $size 4; dimension 3 is 'x.
//   up is logic [0:3]: $increment -1. v is [7:0]: $high 7. q holds three
//   words: $size 3.
//   "ab" < "cd"; {"ab", "cd"} is "abcd"; {2{"ab"}} is "abab"; v[0] is 0,
//   so v[0] ? s1 : s2 is "cd".
//   (v + 8'd1) * 2 - (v - 1) keeps both pairs of parentheses.
module top;
    logic [7:0] v = 8'h5a;
    logic [3:0] mem [2:5];
    logic [0:3] up;
    int q[$] = '{1, 2, 3};
    string s1 = "ab", s2 = "cd";
    wire [31:0] d1, d2, d3, d4, d5, d6, d7, d8, d9;
    wire e1, e2, e3, e4;
    wire [7:0] e5;
    assign d1 = $dimensions(mem);
    assign d2 = $unpacked_dimensions(mem);
    assign d3 = $left(mem);
    assign d4 = $right(mem, 2);
    assign d5 = $size(mem);
    assign d6 = $increment(up);
    assign d7 = $high(v);
    assign d8 = $size(q);
    assign d9 = $left(mem, 3);
    assign e1 = s1 < s2;
    assign e2 = {s1, s2} == "abcd";
    assign e3 = {2{s1}} != "abab";
    assign e4 = (v[0] ? s1 : s2) == "cd";
    assign e5 = (v + 8'd1) * 2 - (v - 1);
endmodule
