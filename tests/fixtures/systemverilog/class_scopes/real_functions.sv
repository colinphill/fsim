// SPDX-License-Identifier: Apache-2.0
// Real math functions (IEEE 1800-2023 20.8.2) are constant expressions
// (11.2.1) when their arguments are, so they give class value parameters:
// equal values are one specialization (6.22.1, 8.25).

class R #(real F = 1.0);
  function real get(); return F; endfunction
endclass

module top;
  R #($sqrt(4.0)) a;
  R #(2.0) b;
  R #($pow(2.0, 3.0)) c;
  R #($ln(1.0)) z;
  R #($sqrt(2.0)) s1;
  R #(1.4142135623730951) s2;
  R #($sqrt(6.25)) h;
  R #(2.5) e;
  initial begin
    a = new;
    b = new;
    c = new;
    z = new;
    s1 = new;
    s2 = new;
    h = new;
    e = new;
    $display("a %0.2f b %0.2f c %0.2f z %0.2f", a.get(), b.get(), c.get(),
        z.get());
    $display("sqrt2 %0.15f same=%0d", s1.get(), $typename(s1) == $typename(s2));
    $display("half %0.2f same=%0d same=%0d", h.get(),
        $typename(h) == $typename(e), $typename(a) == $typename(b));
  end
endmodule
