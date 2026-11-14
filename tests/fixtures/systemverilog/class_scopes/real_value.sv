// SPDX-License-Identifier: Apache-2.0
// A class value parameter of real type (IEEE 1800-2023 8.25, 6.20.2, 11.2.1):
// the default, an explicit literal, a folded constant, an integer actual
// converted to real, and an actual folded from module parameters. Equal values
// are one specialization however they are written (6.22.1): 2.5, 2.50 and
// 5.0/2 name the same class, and so does 2.0 * Y for Y = 1.25.
class R #(real F = 1.0);
  function real get(); return F; endfunction
  function real twice(); return F * 2.0; endfunction
endclass

module sub #(parameter real Y = 2.5);
  R #(Y * 2.0) m;
  R #(2.5) other;
  initial begin
    m = new;
    other = new;
    #2;
    $display("%m Y=%0.2f m=%0.2f twice=%0.2f same=%0d", Y, m.get(), m.twice(),
        $typename(m) == $typename(other));
  end
endmodule

module top;
  R d;
  R #(2.5) e;
  R #(2.50) f;
  R #(5.0 / 2) g;
  R #(2) h;
  sub s1();
  sub #(.Y(1.25)) s2();
  initial begin
    d = new;
    e = new;
    f = new;
    g = new;
    h = new;
    #1;
    e = f;
    f = g;
    g = e;
    $display("d %0.2f %0.2f e %0.2f %0.2f h %0.2f", d.get(), d.twice(), e.get(),
        e.twice(), h.get());
    $display("same %0d %0d dflt %0d", $typename(e) == $typename(f),
        $typename(f) == $typename(g), $typename(d) == $typename(e));
  end
endmodule
