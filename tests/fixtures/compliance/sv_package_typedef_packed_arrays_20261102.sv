// fsim: top=sv_package_typedef_packed_arrays before=sv_package_typedef_packed_arrays_pkg_20261102.sv.in
// SPDX-License-Identifier: Apache-2.0
// Packed arrays over package typedefs and over type parameters, with the
// package compiled separately (IEEE 1800-2023 6.20.3, 7.4.1, 26.3).
module pta_tn #(parameter type T = logic [3:0], parameter int N = 2,
    parameter int W = 8, parameter int E = 4, parameter logic [31:0] V = 'hf0);
  T [N-1:0] x;
  initial begin
    x = '1;
    x[0] = T'(0);
    #1 if ($bits(x) != W || $bits(x[0]) != E || x != V)
      $fatal(1, "%m: T [N-1:0] is %0d bits (%0d per element), %h", $bits(x),
        $bits(x[0]), x);
  end
endmodule
module pta_tm #(parameter type T = logic [2:0], parameter int N = 2,
    parameter int W = 12, parameter logic [31:0] V = 'he3f);
  T [1:0][N-1:0] y;
  initial begin
    y = '1;
    y[1][0] = T'(0);
    #1 if ($bits(y) != W || $bits(y[1]) != W / 2 || y != V)
      $fatal(1, "%m: T [1:0][N-1:0] is %0d bits, %h", $bits(y), y);
  end
endmodule
module pta_m #(parameter type T = logic, parameter int W = 1,
    parameter logic [31:0] V = 1);
  T x;
  initial begin
    x = '1;
    #1 if ($bits(x) != W || x != V)
      $fatal(1, "%m: type actual is %0d bits, %h", $bits(x), x);
  end
endmodule
// A type actual with packed dimensions over the parent's type parameter.
module pta_p #(parameter type U = logic [2:0], parameter int W = 6,
    parameter logic [31:0] V = 'h3f);
  pta_m #(U [1:0], W, V) c();
endmodule
module pta_pp #(parameter type V = bit);
  pta_p #(V [2:0], 2 * 3 * $bits(V), {(2 * 3 * $bits(V)){1'b1}}) p();
endmodule
module pta_sub(input logic [3:0] a);
endmodule
module sv_package_typedef_packed_arrays;
  import pta_q::*;
  nib_t [1:0] nib;
  pta_q::nib_t [2:0] qual;
  pair_t [1:0] pair;
  e_t [1:0] enums;
  b_t [3:0] bits;
  en_t [1:0] ints;
  s_t s;
  pta_q::s_t t;
  localparam arr_t P = '{8'h11, 8'h22};
  arr_t arr = '{8'h33, 8'h44};
  pta_q::cls h = new;
  cls g = new;
  logic [7:0] stream;
  typedef struct packed { pta_q::nib_t a; logic [3:0] b; } member_t;
  member_t member;
  wire [3:0] literal;
  pta_sub lit(.a(literal));
  assign literal = B;
  pta_tn a();
  pta_tn #(logic [2:0], 3, 9, 3, 'h1f8) b();
  pta_tn #(pair_t, 3, 24, 8, 'hffff00) c();
  pta_tn #(bit, 5, 5, 1, 'h1e) d();
  pta_tn #(e_t, 2, 8, 4, 'hf0) e();
  pta_tm ma();
  pta_tm #(pta_q::nib_t, 3, 24, 'hff0fff) mb();
  pta_m #(pta_q::nib_t [1:0], 8, 'hff) qa();
  pta_m #(nib_t [2:0], 12, 'hfff) qb();
  pta_p pa();
  pta_p #(logic [4:0], 10, 'h3ff) pb();
  pta_pp ppa();
  pta_pp #(logic [1:0]) ppb();
  sv_package_typedef_packed_arrays_reexport rx();
  initial begin
    nib_t [1:0] local_nib;
    nib = 8'hA5;
    qual = 12'h123;
    pair = '0;
    pair[1].hi = 4'h3;
    pair[0].lo = 4'hc;
    enums[1] = B;
    enums[0] = A;
    bits = 4'b1010;
    ints[1] = C1;
    ints[0] = C0;
    s = "hi";
    t = "abc";
    stream = {<< nib_t {8'h12}};
    member = 8'hc3;
    local_nib = 8'h5a;
    #2;
    if ($bits(nib) != 8 || nib[1] != 4'hA || nib[0] != 4'h5)
      $fatal(1, "nib_t [1:0]: %0d bits, %h", $bits(nib), nib);
    if ($bits(qual) != 12 || qual[2] != 4'h1 || qual[0] != 4'h3)
      $fatal(1, "pta_q::nib_t [2:0]: %0d bits, %h", $bits(qual), qual);
    if ($bits(pair) != 16 || pair != 16'h300c || pair[1].hi != 4'h3)
      $fatal(1, "pair_t [1:0]: %0d bits, %h", $bits(pair), pair);
    if ($bits(enums) != 8 || enums != 8'h93)
      $fatal(1, "e_t [1:0]: %0d bits, %h", $bits(enums), enums);
    if ($bits(bits) != 4 || bits[1] != 1'b1 || bits[0] != 1'b0)
      $fatal(1, "b_t [3:0]: %0d bits, %b", $bits(bits), bits);
    if ($bits(ints) != 64 || ints != 64'h1_0000_0000)
      $fatal(1, "en_t [1:0]: %0d bits, %h", $bits(ints), ints);
    if (s.len() != 2 || t.len() != 3)
      $fatal(1, "s_t: %0d %0d", s.len(), t.len());
    if (P[0] != 8'h11 || P[1] != 8'h22 || arr[0] != 8'h33 || arr[1] != 8'h44)
      $fatal(1, "arr_t: %h %h %h %h", P[0], P[1], arr[0], arr[1]);
    if (h.v != 7 || g.v != 7)
      $fatal(1, "cls: %0d %0d", h.v, g.v);
    if (stream != 8'h21)
      $fatal(1, "stream slice nib_t: %h", stream);
    if ($bits(member) != 8 || member.a != 4'hc || member.b != 4'h3)
      $fatal(1, "member nib_t: %h %h", member.a, member.b);
    if (literal != 4'd9)
      $fatal(1, "enum literal port: %0d", literal);
    if ($bits(local_nib) != 8 || local_nib[1] != 4'h5)
      $fatal(1, "procedural nib_t [1:0]: %h", local_nib);
    $display("PASS");
  end
endmodule
module sv_package_typedef_packed_arrays_reexport;
  import pta_r::*;
  nib_t [1:0] z;
  initial begin
    z = '1;
    if ($bits(z) != 8 || P4 != 4) $fatal(1, "re-export: %0d %0d", $bits(z), P4);
  end
endmodule
