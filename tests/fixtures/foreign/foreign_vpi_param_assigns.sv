// SPDX-License-Identifier: Apache-2.0
// The parameter assignments of module instances and the def params of a
// module (IEEE 1800-2023 23.10, 37.28).
module sub #(parameter int W = 1, parameter int D = 2, parameter type T = bit) ();
    T v;
endmodule

module top;
    localparam int K = 6;
    // Ordered association: W and D, the second an expression seen from top.
    sub #(4, K + 1) u_ord();
    // Named association (vpiConnByName), with a type parameter's actual.
    sub #(.D(9), .T(logic [3:0])) u_named();
    // No overrides: no parameter assignments (37.28 detail 4); the defparam
    // of top sets W (23.10.1) and is a def param object of top.
    sub u_def();
    defparam u_def.W = 11;
    initial #1;
endmodule
