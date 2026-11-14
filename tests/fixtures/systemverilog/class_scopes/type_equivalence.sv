// SPDX-License-Identifier: Apache-2.0
// Specializations with equal parameter values are one type, however the
// values are written (IEEE 1800-2023 6.22.1, 8.25): C#(M) with M from a
// module parameter expression and C#(2 + 2) name the same class, so handles
// of them are assigned to and from each other and have the same $typename.
// No literal with that value appears in the design.
class C #(int W = 6);
  int v;
  function int width(); return W; endfunction
endclass

module sub #(parameter int P = 1);
  localparam int M = P * 2 + P * 2;
  C #(M) a;
  C #(2 + 2) b;
  C c;
  initial begin
    a = new;
    b = new;
    c = new;
    a.v = 7;
    b = a;
    a = b;
    $display("P=%0d eq=%0d v=%0d width=%0d same=%0d dflt=%0d", P, a == b, b.v,
        a.width(), $typename(a) == $typename(b),
        $typename(a) == $typename(c));
  end
endmodule

module top;
  sub #(.P(1)) u1();
  sub u2();
endmodule
