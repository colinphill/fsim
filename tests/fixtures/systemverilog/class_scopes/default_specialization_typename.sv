// SPDX-License-Identifier: Apache-2.0
// The bare name of a parameterized class denotes its default specialization
// (IEEE 1800-2023 8.25, 6.22.1), the type `C #(defaults)` also names, so
// $typename (20.6.1) prints both the same.
class A #(real X = 2.5, int N = 3);
  function real get(); return X; endfunction
endclass

class D #(type T = int, int W = 2);
endclass

class E;
endclass

module top;
  A a0;
  A #(2.5, 3) a1;
  A #(2.50) a2;
  D d0;
  D #(int, 2) d1;
  E e0;
  A #(3) a4;
  A #(3.0, 3) a5;
  A #(.N(3)) a6;
  A #(.X()) a7;
  typedef A #(3) at_t;
  at_t a3;
  typedef at_t at2_t;
  at2_t a8;
  initial begin
    $display("%s", $typename(a0));
    $display("%s", $typename(a1));
    $display("%s", $typename(a2));
    $display("%s", $typename(d0));
    $display("%s", $typename(d1));
    $display("%s", $typename(e0));
    $display("%s", $typename(a3));
    $display("%s", $typename(a4));
    $display("%s", $typename(a8));
    $display("int-real %0d named %0d defaulted %0d typedef %0d",
        $typename(a4) == $typename(a5), $typename(a6) == $typename(a0),
        $typename(a7) == $typename(a0), $typename(a3) == $typename(a8));
    $display("same %0d %0d %0d %0d", $typename(a0) == $typename(a1),
        $typename(a4) == $typename(a3), $typename(d0) == $typename(d1),
        $typename(a0) == $typename(a2));
  end
endmodule
