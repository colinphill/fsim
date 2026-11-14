// SPDX-License-Identifier: Apache-2.0
// A class value parameter is real by its type after typedef resolution, not
// by the way the type is written (IEEE 1800-2023 6.18, 6.20.2, 8.25): a
// typedef of real, a package typedef of real, a package typedef of shortreal
// (whose values round to float), and a type parameter bound to real or
// shortreal. An integer actual converts to the real type.
package pr;
  typedef real pk_t;
  typedef shortreal pf_t;
endpackage

typedef real r_t;

class A #(r_t X = 2.5);
  function real get(); return X; endfunction
endclass

class B #(pr::pk_t X = 1.5);
  function real get(); return X; endfunction
endclass

class S #(pr::pf_t X = 0.1);
  function real get(); return X; endfunction
endclass

class T #(type R = real, R X = 3.5);
  function real get(); return X; endfunction
endclass

module top;
  A a0;
  A #(3) a3;
  A #(2.5) a25;
  A #(2.50) a250;
  B b0;
  B #(2) b2;
  S s0;
  T t0;
  T #(real, 4.25) t1;
  T #(shortreal, 0.1) t2;
  initial begin
    a0 = new;
    a3 = new;
    a25 = new;
    a250 = new;
    b0 = new;
    b2 = new;
    s0 = new;
    t0 = new;
    t1 = new;
    t2 = new;
    a25 = a250;
    $display("a %0.2f %0.2f same=%0d", a0.get(), a3.get(),
        $typename(a25) == $typename(a250));
    $display("b %0.2f %0.2f", b0.get(), b2.get());
    $display("s %0.9f", s0.get());
    $display("t %0.2f %0.2f %0.9f", t0.get(), t1.get(), t2.get());
  end
endmodule
