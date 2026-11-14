// SPDX-License-Identifier: Apache-2.0
// Let declarations and let expressions seen through VPI (IEEE 1800-2023
// 11.12, 37.12, 37.57). A scope iterates its let declarations; a
// declaration has its formals as seq formal decls (the typed one with its
// typespec, the defaulted one with its default) and its body, where a
// formal is a ref obj of the formal decl. A call or name of a let is a let
// expression: its vpiLetDecl, its arguments in the order of the formals
// with a default where none is given (37.57 detail 1), and the value of
// the body with the formals bound to the arguments. At 1: a = 3, b = 5, so
// sum(a, b) = 8, sum(.x(a)) = 3 + 1 = 4, both = 3 & 5 = 1 and
// sum(sum(a, 2), b) = 10: a let call as an argument of a let call is
// expanded in the caller's context, so it is no recursion (11.12). The
// nets hold the same values, and each let expression decompiles to its
// source form (37.59 detail 2).
module top;
    typedef logic [3:0] nib_t;
    logic [3:0] a = 4'd3, b = 4'd5;
    let sum(x, nib_t y = 4'd1) = x + y;
    let both = a & b;
    wire [3:0] w1, w2, w3, w4;
    assign w1 = sum(a, b);
    assign w2 = sum(.x(a));
    assign w3 = both;
    assign w4 = sum(sum(a, 4'd2), b);
    // Arguments that spell the formals' names are the caller's objects
    // (11.12): diff(y, x) is 4 - 9, 4'd11.
    logic [3:0] x = 4'd9, y = 4'd4;
    let diff(x, y) = x - y;
    wire [3:0] w5;
    assign w5 = diff(y, x);
endmodule
