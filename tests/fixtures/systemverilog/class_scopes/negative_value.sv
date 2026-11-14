// SPDX-License-Identifier: Apache-2.0
// A class value parameter folded to a negative value from module parameters
// (IEEE 1800-2023 8.25, 6.20.2): C#(N) with N = A - B = -3 reads -3, the same
// specialization as C#(-3), whose handles are assigned to each other.
class C #(int W = 6);
  int v;
  function int width(); return W; endfunction
  function int twice(); return W * 2; endfunction
endclass

module sub #(parameter int A = 1, parameter int B = 4);
  localparam int N = A - B;
  C #(N) a;
  C #(-3) b;
  initial begin
    a = new;
    b = new;
    b = a;
    $display("N=%0d width=%0d twice=%0d same=%0d", N, a.width(), a.twice(),
        $typename(a) == $typename(b));
  end
endmodule

module top;
  sub s();
endmodule
