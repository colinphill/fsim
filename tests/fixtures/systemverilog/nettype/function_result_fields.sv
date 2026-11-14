// SPDX-License-Identifier: Apache-2.0
// members of a function's result variable written in a loop (13.4.1), used by struct resolvers
module top;
  typedef struct { real field1; bit field2; } T;
  function automatic T f(input real a[]);
    f.field1 = 0.0;
    foreach (a[i]) begin f.field1 += a[i]; $display("  step %0d f.field1=%f", i, f.field1); end
  endfunction
  function automatic T g(input real a[]);
    T r;
    r.field1 = 0.0;
    foreach (a[i]) r.field1 += a[i];
    return r;
  endfunction
  real xs[] = '{1.5, 2.25};
  T t, u;
  initial begin t = f(xs); u = g(xs); $display("f=%f g=%f", t.field1, u.field1); end
endmodule
