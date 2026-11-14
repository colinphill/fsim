// SPDX-License-Identifier: Apache-2.0
// A user constant function (IEEE 1800-2023 13.4.3) may take and return real
// values (11.2.1): its real formals, locals and result convert as the
// declared real type does (6.12.2, 6.24, 11.8.2), so a class value parameter
// binds the function's real result and names the same specialization as
// the literal of that value (8.25, 20.6.1).
function real half(real x);
  return x / 2.0;
endfunction

function real poly(real x);
  real t;
  t = x * x;
  t = t + 1;
  return t;
endfunction

function real from_int(int n);
  return n;
endfunction

function shortreal narrow(real x);
  return x;
endfunction

function int rounded(real x);
  real twice = x * 2.0;
  return int'(twice);
endfunction

function int rtoi3(real x);
  return $rtoi(x * 3.0);
endfunction

function real chain(real x);
  return half(poly(x)) + $sqrt(half(32.0));
endfunction

function int dbl(int x);
  return x * 2;
endfunction

localparam real Q = half(5.0);
localparam int QI = dbl(5);

class C #(real X = 1.0);
  function real get(); return X; endfunction
endclass

class N #(int V = 0);
  function int get(); return V; endfunction
endclass

module top;
  C #(half(5.0)) c1;
  C #(2.5) c2;
  C #(Q) c3;
  C #(poly(3.0)) c4;
  C #(from_int(3)) c5;
  C #(half(5)) c6;
  C #(chain(3.0)) c7;
  C #(narrow(0.1)) c8;
  N #(rounded(1.25)) n1;
  N #(3) n2;
  N #(QI) n3;
  N #(rtoi3(1.5)) n4;
  initial begin
    c1 = new;
    c2 = new;
    c3 = new;
    c4 = new;
    c5 = new;
    c6 = new;
    c7 = new;
    c8 = new;
    n1 = new;
    n2 = new;
    n3 = new;
    n4 = new;
    c2 = c1;
    $display("half %0.2f %0.2f %0.2f", c1.get(), c3.get(), c6.get());
    $display("poly %0.2f from_int %0.2f chain %0.2f", c4.get(), c5.get(),
        c7.get());
    $display("narrow %0.9f", c8.get());
    $display("rounded %0d unit %0d rtoi %0d", n1.get(), n3.get(), n4.get());
    $display("same %0d %0d %0d", $typename(c1) == $typename(c2),
        $typename(c1) == $typename(c3), $typename(n1) == $typename(n2));
  end
endmodule
