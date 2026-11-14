// SPDX-License-Identifier: Apache-2.0
//
// The variable a nonblocking assignment writes is the innermost declaration of
// its root identifier visible at the statement (IEEE 1800-2023 6.21, 10.4.2,
// 23.9), so a name that is automatic in one block can be static in a sibling
// block, and a module variable is a legal target wherever no local hides it.
module top;
  int g = 1;
  int t = 100;
  int count = 0;

  function automatic int f(int v);
    int g;
    begin
      static int t;
      count <= count + 1;
      $display("sns scopes static-before %0d", t);
      t <= v + 10;
    end
    begin
      int t;
      t = v + 1;
      g = t;
    end
    begin
      begin
        int u;
        u = 5;
      end
      t <= v;
    end
    return g;
  endfunction

  initial begin
    int r;
    r = f(7);
    $display("sns scopes r=%0d g=%0d t=%0d count=%0d", r, g, t, count);
    #1;
    $display("sns scopes next t=%0d count=%0d", t, count);
    r = f(8);
    $display("sns scopes r=%0d t=%0d count=%0d", r, t, count);
    #1;
    $display("sns scopes last t=%0d count=%0d", t, count);
  end
endmodule
