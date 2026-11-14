// SPDX-License-Identifier: Apache-2.0
// A method called on a member whose type is a class type parameter runs the
// method of the class the specialization binds the parameter to (IEEE
// 1800-2023 8.25), here C#(7) and C#(9).
class C #(int W = 4);
  int v;
  function int width(); return W; endfunction
  function void set(int x); v = x; endfunction
  function int get(); return v; endfunction
endclass

package pa;
  typedef C #(6) T;
endpackage

import pa::T;

class D #(type T = int);
  T inner;
  function new(); inner = new; endfunction
  function int w(); return inner.width(); endfunction
  function int roundtrip(int x); inner.set(x); return inner.get(); endfunction
  // A typedef of a class nested in D hides the type parameter there (6.18,
  // 8.25).
  class Nest;
    typedef C #(5) T;
    function int run();
      T h;
      h = new;
      h.set(2);
      return h.get() + h.width();
    endfunction
  endclass
  function int nested();
    Nest n;
    n = new;
    return n.run();
  endfunction
  // A package import in the function body hides the type parameter T from
  // there on (A.2.1.3, 26.3, 6.18): `T` is pa::T, the class C#(6).
  function int imported_block();
    import pa::T;
    T loc;
    loc = new;
    loc.set(1);
    return loc.get() + loc.width();
  endfunction
  // The compilation unit imports pa::T, but the class's own parameter T hides
  // it inside D (26.3, 8.25): `inner.width()` is the bound class's.
  function int imported_hidden();
    return inner.width();
  endfunction
  // A typedef of the method hides the type parameter T there (6.18): the
  // local handle is a C#(3), while `inner` stays the bound class.
  function int shadow();
    typedef C #(3) T;
    T loc;
    loc = new;
    loc.set(4);
    return loc.get() + loc.width();
  endfunction
endclass

module top;
  D #(C #(7)) d;
  D #(C #(9)) e;
  initial begin
    d = new;
    e = new;
    $display("d.w=%0d e.w=%0d rt=%0d sh=%0d nest=%0d imp=%0d", d.w(), e.w(),
        d.roundtrip(5), e.shadow(), e.nested(), e.imported_hidden());
    $display("block=%0d", e.imported_block());
  end
endmodule
