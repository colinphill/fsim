// SPDX-License-Identifier: Apache-2.0
// Property and sequence declarations, instances, formals, specs and the
// operators of their expressions seen through VPI (IEEE 1800-2023 37.50 -
// 37.54). The formals x and y are no objects of the module, so a use of one
// is a reference object named like it.
module top;
    logic clk = 1'b0;
    logic a = 1'b0, b = 1'b0, r = 1'b0;

    sequence s_ab(x, y);
        x ##1 y;
    endsequence

    property p_imp(x, y);
        @(posedge clk) disable iff (r) s_ab(x, y) |=> not y;
    endproperty

    property p_until(x, y = b);
        @(posedge clk) x until y;
    endproperty

    // Local variables are variables of the declaration (37.51, 37.53).
    sequence s_lv;
        int v;
        logic [3:0] w;
        a ##1 b;
    endsequence

    a1: assert property (p_imp(a, b));
    a2: assert property (p_until(a));
    a3: assert property (@(posedge clk) a |-> ##[1:2] b);
endmodule
