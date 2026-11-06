// fsim: top=sv_typedef_string_declarations std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.16, 6.18, 6.24.1: a declaration whose type is a typedef of
// string, directly or through another typedef, is a string: variables,
// parameters, local parameters, function results and arguments, class
// properties and casts all follow the typedef.
typedef string str_t;
typedef str_t str2_t;

class holder;
  str_t name = "cls";
  function new(str_t n);
    name = n;
  endfunction
  function str_t shout();
    return {name, "!"};
  endfunction
endclass

module child #(parameter str_t P = "param", parameter str2_t EXPECT = P);
  initial begin
    if (P != EXPECT) $fatal(1, "string typedef parameter");
  end
endmodule

module sv_typedef_string_declarations;
  str_t v;
  str2_t w;
  string plain;
  localparam str_t LP = "local";
  localparam str2_t LQ = "chain";
  holder h;

  function automatic str_t join_text(str_t a, str2_t b);
    return {a, "-", b};
  endfunction
  function automatic str2_t twice(str_t a);
    return {a, a};
  endfunction

  child c_default();
  child #(.P("over"), .EXPECT("over")) c_override();

  initial begin
    v = "abc";
    w = v;
    plain = w;
    if (plain != "abc") $fatal(1, "assignment between typedef strings");
    plain = "xyz";
    v = str_t'(plain);
    if (v != "xyz") $fatal(1, "cast to typedef string");
    w = str2_t'(16'h4849);
    if (w != "HI") $fatal(1, "cast of a packed value to a chained typedef");
    plain = join_text(v, "q");
    if (plain != "xyz-q") $fatal(1, "argument and result");
    w = twice("ab");
    if (w != "abab") $fatal(1, "chained typedef result");
    if (LP != "local") $fatal(1, "typedef string localparam");
    if (LQ != "chain") $fatal(1, "chained typedef localparam");
    h = new("obj");
    if (h.shout() != "obj!") $fatal(1, "class property and result");
    $display("PASS");
  end
endmodule
