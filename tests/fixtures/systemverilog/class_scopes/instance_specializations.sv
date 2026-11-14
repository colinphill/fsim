// SPDX-License-Identifier: Apache-2.0
// A class specialization whose actuals read the parameters of the design
// element instance that names it is that instance's: C#(N) in sub#(3) is
// C#(3) and in sub#(5) is C#(5) (IEEE 1800-2023 8.25, 23.2.2), also through a
// localparam that reads the parameter.
class C #(int W = 4);
  rand bit [W-1:0] v;
  int seen;
  function void post_randomize(); seen = W; endfunction
  function int width(); return W; endfunction
endclass

module sub #(parameter int N = 2);
  localparam int M = N + 1;
  C #(N) c;
  C #(M) d;
  initial begin
    #N;
    c = new;
    d = new;
    void'(c.randomize());
    void'(d.randomize());
    $display("sub N=%0d seen=%0d width=%0d bits=%0d next=%0d/%0d",
        N, c.seen, c.width(), $bits(c.v), d.width(), d.seen);
  end
endmodule

// The value of N is no literal of the design (22 and 36 come from `P * 7 + 1`
// and `Q + 1`), and two modules reach each value through different
// expressions: every instance still reads its own value (8.25).
module sp #(parameter int P = 2, parameter int T = 0);
  localparam int N = P * 7 + 1;
  C #(N) c;
  initial begin
    #(100 + T);
    c = new;
    void'(c.randomize());
    $display("sp P=%0d N=%0d width=%0d seen=%0d bits=%0d",
        P, N, c.width(), c.seen, $bits(c.v));
  end
endmodule

module sq #(parameter int Q = 14, parameter int T = 0);
  localparam int N = Q + 1;
  C #(N) c;
  initial begin
    #(100 + T);
    c = new;
    void'(c.randomize());
    $display("sq Q=%0d N=%0d width=%0d seen=%0d bits=%0d",
        Q, N, c.width(), c.seen, $bits(c.v));
  end
endmodule

module top;
  sp #(.P(3), .T(1)) s1();
  sq #(.Q(21), .T(2)) s2();
  sp #(.P(5), .T(3)) s3();
  sq #(.Q(35), .T(4)) s4();
  sub #(3) u3();
  sub #(5) u5();
  sub u2();
endmodule
