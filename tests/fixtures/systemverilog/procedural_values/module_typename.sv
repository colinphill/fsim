// SPDX-License-Identifier: Apache-2.0
// $typename names the resolved type (IEEE 1800-2023 20.6.1): a typedef is
// resolved to the type it denotes (a), the default signing is dropped and
// another one named (b), an unpacked array is `$` before its ranges (d),
// enumeration constants carry their encoded values (e), and a user-defined
// type is prefixed with the package or scope that declares it (f); ranges
// are unsized decimals (g) and spaces separate only identifiers and
// keywords (h). The normative steps decide where the clause's example
// differs: step h gives "bit[2:0]" where the example shows "bit [2:0]", and
// step a resolves a package typedef of bit [9:1] to "bit[9:1]" where the
// example shows "A::bit[9:1]".
package A;
  typedef enum {AA, BB, CC = 99} e_t;
  typedef bit [9:1] word;
endpackage
module top;
  typedef bit node;
  typedef logic [3:0] nib;
  typedef struct packed { logic [3:0] a; bit b; } ps_t;
  typedef struct { node x, y; } us_t;
  typedef enum logic [1:0] {R, G, B} c_t;
  node [2:0] X;
  int signed Y;
  int unsigned U;
  bit signed [3:0] S;
  nib n;
  A::word w;
  A::e_t e;
  ps_t ps;
  us_t AB[10];
  c_t c;
  int q[$];
  int d[];
  string s;
  real r;
  initial begin
    $display("1 %s", $typename(X));
    $display("2 %s", $typename(Y));
    $display("3 %s", $typename(U));
    $display("4 %s", $typename(S));
    $display("5 %s", $typename(n));
    $display("6 %s", $typename(nib));
    $display("7 %s", $typename(w));
    $display("8 %s", $typename(e));
    $display("9 %s", $typename(ps));
    $display("10 %s", $typename(AB));
    $display("11 %s", $typename(c));
    $display("12 %s", $typename(q));
    $display("13 %s", $typename(d));
    $display("14 %s", $typename(s));
    $display("15 %s", $typename(r));
    $display("16 %s", $typename(n + 1));
  end
endmodule
