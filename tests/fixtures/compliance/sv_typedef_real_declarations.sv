// fsim: top=sv_typedef_real_declarations std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.18, 6.12: a declaration whose type is a typedef of real,
// shortreal or realtime is a real object: variables, ports, function
// results and arguments, class properties, type parameters, casts and
// arithmetic all follow the typedef. Class property initializers follow
// the declared type too (8.5), including plain real properties and
// literals.
typedef real r_t;
typedef shortreal s_t;
typedef realtime rt_t;
typedef r_t rr_t;

class box;
  r_t x = 1.5;
  function new(r_t a);
    x = a;
  endfunction
  function r_t twice();
    return x * 2.0;
  endfunction
endclass

class start;
  real a = 1.5;
  real d = 3;
  shortreal s = 0.75;
  realtime t = 2.5;
  r_t z = 1.25;
  int u = 1_000;
  static r_t shared = 0.5;
  function r_t get_z();
    return z;
  endfunction
endclass

module child(input r_t p, output r_t q);
  always_comb q = p * 2.0;
endmodule

module generic_leaf #(parameter type T = real);
  T x;
  initial begin
    x = 2.5;
    if (x * 2.0 != 5.0) $fatal(1, "type parameter real arithmetic");
    if ($realtobits(x) != 64'h4004000000000000)
      $fatal(1, "type parameter real bits");
  end
endmodule

module sv_typedef_real_declarations;
  r_t v;
  s_t sv;
  rt_t tv;
  rr_t cv;
  r_t pin = 1.25;
  r_t qout;
  real viewed;
  box b;

  function automatic r_t half(r_t a);
    return a / 2;
  endfunction
  function automatic r_t from_int(int i);
    return i;
  endfunction

  child u(.p(pin), .q(qout));
  generic_leaf #(.T(r_t)) gl();
  generic_leaf #(.T(rr_t)) glc();

  localparam r_t RP = 2.5;
  start st;

  initial begin
    if (RP != 2.5 || $realtobits(RP) != 64'h4004000000000000)
      $fatal(1, "typedef real local parameter");
    st = new;
    if (st.a != 1.5) $fatal(1, "real property literal initializer");
    if (st.d != 3.0) $fatal(1, "integer literal into real property");
    if (st.s != 0.75) $fatal(1, "shortreal property initializer");
    if (st.t != 2.5) $fatal(1, "realtime property initializer");
    if (st.z != 1.25 || st.get_z() / 5 != 0.25)
      $fatal(1, "typedef real property initializer");
    if (st.u != 1000) $fatal(1, "underscored literal initializer");
    if (start::shared * 4 != 2.0) $fatal(1, "static typedef real property");
    v = 2.5;
    if (v * 2.0 != 5.0) $fatal(1, "variable arithmetic");
    if (v / 2 != 1.25) $fatal(1, "variable real division");
    if ($realtobits(v * 2.0) != 64'h4014000000000000)
      $fatal(1, "realtobits of product");
    if ($realtobits(v) != 64'h4004000000000000) $fatal(1, "realtobits");
    v = 3;
    if (v / 2 != 1.5) $fatal(1, "integer assigned to real typedef");
    sv = 1.5;
    if (sv * 2.0 != 3.0) $fatal(1, "shortreal typedef arithmetic");
    if ($shortrealtobits(sv) != 32'h3fc00000) $fatal(1, "shortrealtobits");
    tv = 2.5;
    if ($realtobits(tv) != 64'h4004000000000000) $fatal(1, "realtime bits");
    cv = 2.5;
    if (cv / 2 != 1.25) $fatal(1, "chained typedef");
    if (half(5) != 2.5) $fatal(1, "argument and result conversion");
    if (from_int(3) / 2.0 != 1.5) $fatal(1, "result conversion");
    if (r_t'(7) / 2 != 3.5) $fatal(1, "cast to typedef real");
    viewed = half(2.5);
    if (viewed != 1.25) $fatal(1, "result stored in real");
    b = new(2.5);
    if (b.twice() != 5.0) $fatal(1, "class property");
    if (b.x / 2 != 1.25) $fatal(1, "class property division");
    #1;
    if (qout != 2.5) $fatal(1, "port arithmetic");
    $display("PASS");
  end
endmodule
