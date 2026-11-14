// SPDX-License-Identifier: Apache-2.0
// The match items of a sequence operand written `(s, item, ...)` seen
// through VPI (IEEE 1800-2023 16.10, 37.54 vpiMatchItem): assignments to
// local variables, increments and subroutine calls, in order. The local
// variable v is no object of the module, so a use of it is a reference
// object named like it. An operand that is a plain name with match items,
// `(a, w = d)`, is an occurrence of its own: a reference object named like
// it whose vpiActual is the object (37.15), holding that occurrence's items;
// a constant operand `(1, w++)` is an expression of its own.
module top;
    logic clk = 1'b0;
    logic a = 1'b0, b = 1'b0;
    int d = 0;

    sequence s_mi;
        int v;
        (a ##1 b, v = d, v++, $display("v=%0d", v)) ##1 (d == v);
    endsequence

    sequence s_pn;
        int w;
        (a, w = d) ##1 (1, w++) ##1 (b && w == 1);
    endsequence

    c1: cover property (@(posedge clk) (a && b, $display("hit")));
endmodule
