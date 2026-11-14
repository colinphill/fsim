// SPDX-License-Identifier: Apache-2.0
// A package-scoped typedef name is looked up in its own package (IEEE
// 1800-2023 26.3): two packages declare `t` as different types, and a class
// value parameter is real only when the `t` of the package it names is.
// An actual converts to the parameter's type (6.20.2, 6.24, 11.8.2): the
// real 2.5 rounds to 3 for the int `p2::t`, and 0.1 rounds to a float only
// for the shortreal `p3::t`, not for the real `p4::t`.
package p1;
  typedef real t;
endpackage

package p2;
  typedef int t;
endpackage

package p3;
  typedef shortreal t;
endpackage

package p4;
  typedef real t;
endpackage

class S3 #(p3::t X = 0.1);
  function real get(); return X; endfunction
endclass

class S4 #(p4::t X = 0.1);
  function real get(); return X; endfunction
endclass

class A #(p1::t X = 2.5);
  function real get(); return X; endfunction
endclass

class B #(p2::t X = 2);
  function int get(); return X; endfunction
endclass

module top;
  A a0;
  A #(7) a7;
  B b0;
  S3 s3;
  S4 s4;
  B #(2.5) b25;
  initial begin
    a0 = new;
    a7 = new;
    b0 = new;
    s3 = new;
    s4 = new;
    b25 = new;
    $display("a %0.2f %0.2f", a0.get(), a7.get());
    $display("b %0d %0d", b0.get(), b25.get());
    $display("s %0.9f %0.9f", s3.get(), s4.get());
  end
endmodule
