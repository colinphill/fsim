// SPDX-License-Identifier: Apache-2.0
// Class scope names resolve by identity: a class scope's members include
// the typedefs it inherits (IEEE 1800-2023 8.13); a type parameter
// qualifier names the class its actual binds, hiding a class of the same
// name, and selects through that class's nested classes (8.23, 8.25); a
// cast default of a value parameter gives the specialization of the equal
// explicit actual (8.25); a static variable of a parameterized class's
// static method is one per specialization (6.21).
class Base;
  typedef logic [5:0] word_t;
endclass

class Derived extends Base;
endclass

class C;
  typedef logic [1:0] T;
  class nst;
    typedef logic [2:0] S;
  endclass
endclass

class D;
  typedef logic [3:0] T;
  class nst;
    typedef logic [4:0] S;
  endclass
endclass

class P #(type C);
  localparam type C1_t = C::T;
  localparam type C2_t = C::nst::S;
  C1_t x = '1;
  C2_t y = '1;
endclass

class Cast #(parameter int N = int'(42));
  static int made;
  function new();
    made++;
  endfunction
  function int get();
    return N;
  endfunction
endclass

class Single #(type T = int);
  T v;
  static function Single #(T) self();
    static Single #(T) c = new;
    return c;
  endfunction
endclass

module top;
  Derived::word_t w;
  P #(C) pc = new;
  P #(D) pd = new;
  Cast #() a;
  Cast #(42) b;
  initial begin
    w = '1;
    $display("inherited %0d", $bits(w));
    $display("qualified %0d %0d %0d %0d",
      $bits(pc.x), $bits(pc.y), $bits(pd.x), $bits(pd.y));
    a = new;
    b = new;
    $display("cast %0d %0d made %0d", a.get(), b.get(), Cast#(42)::made);
    $display("single %0d %0d %0d",
      Single#(int)::self() == Single#(int)::self(),
      Single#(bit)::self() == Single#(bit)::self(),
      Single#(int)::self() != null);
  end
endmodule
