// SPDX-License-Identifier: Apache-2.0
// Clocked properties, multiclock sequence expressions and case properties
// seen through VPI (IEEE 1800-2023 37.52, 37.56). Expected objects, derived
// from the figures and details of those clauses:
//   p_case    case property: vpiCondition c; three case property items. The
//             first has the value 1'b0 and the property a, the second the
//             values 1'b1 and 1'bx and the property b, the default item has
//             no value (detail 5) and the property `not a`.
//   p_nested  `a |-> @(posedge clk2) b`: the operation |-> over a and a
//             clocked property whose clocking event is posedge clk2 and
//             whose property expression is b.
//   s_multi   `@(posedge clk) a ##1 @(posedge clk2) b` joins sequences on two
//             clocks, so its expression is a multiclock sequence expression
//             of two clocked seq objects: a on posedge clk (the clock of the
//             declaration) and b on posedge clk2.
module top;
    logic clk = 1'b0, clk2 = 1'b0;
    logic a = 1'b0, b = 1'b0, c = 1'b0;

    property p_case;
        @(posedge clk) case (c)
            1'b0: a;
            1'b1, 1'bx: b;
            default: not a;
        endcase
    endproperty

    property p_nested;
        @(posedge clk) a |-> @(posedge clk2) b;
    endproperty

    sequence s_multi;
        @(posedge clk) a ##1 @(posedge clk2) b;
    endsequence
endmodule
