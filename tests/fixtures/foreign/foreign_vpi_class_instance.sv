// SPDX-License-Identifier: Apache-2.0
// The class typespec of `C #(N) c;` (and of `C #(M) d;` through a localparam)
// in each instance of a module names the specialization that instance's
// parameter selects (IEEE 1800-2023 8.25, 23.2.2, 37.32).
class C #(int W = 4);
  int seen;
endclass

module sub #(parameter int N = 2);
  localparam int M = N + 1;
  C #(N) c;
  C #(M) d;
  initial begin
    c = new;
    d = new;
  end
endmodule

module top;
  sub #(3) u3();
  sub #(5) u5();
  sub u2();
endmodule
