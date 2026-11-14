// SPDX-License-Identifier: Apache-2.0
// Property and sequence declarations, instances, formals, specs and the
// operators of their expressions seen through VPI (IEEE 1800-2023 37.50 -
// 37.54). The formals x and y are no objects of the module, so a use of one
// is a reference object named like it.

// A checker instance's declarations are specialized into the instantiating
// scope (17.3) but are not declarations written there: the scope's
// property declarations do not list them.
checker chk(logic x, logic c);
    property cp;
        @(posedge c) x;
    endproperty
    ca: assert property (cp);
endchecker

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

    // Typed formals and local variables have the typespec of their type
    // (37.51, 37.53 vpiTypespec): a typedef's, a built-in one's, an array's.
    typedef logic [7:0] byte_t;
    sequence s_ty(int n, byte_t t, logic [3:0] m);
        byte_t lv;
        int q [2];
        a ##1 b;
    endsequence

    a1: assert property (p_imp(a, b));
    a2: assert property (p_until(a));
    a3: assert property (@(posedge clk) a |-> ##[1:2] b);
    // strong() and weak() over a boolean leaf: the leaf is its own ref obj
    // with the strength (16.12.2, 37.52 detail 3).
    a4: assert property (@(posedge clk) strong(a) and weak(b));
    // An inline cover sequence: its property is a spec with the clock,
    // the disable condition and the sequence (37.50, 37.52).
    a5: cover sequence (@(posedge clk) disable iff (r) a ##1 b);
    chk u_chk(a, clk);
endmodule
