// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 8.16, 8.25: each specialization of a parameterized class
// is a distinct type, so $cast of a box#(4) object to a box#(7) or to the
// default specialization box#(1) fails and leaves the destination
// unchanged, while $cast to box#(4), to a typedef of it and to the common
// base succeeds. The module casts in its own process, probe::check in a
// class method the module calls, and post_randomize in the class method
// executor.
class base;
endclass

class box #(int P = 1) extends base;
  function int value();
    return P;
  endfunction
endclass

typedef box #(4) box4;

class probe;
  rand bit [1:0] x;
  int results;
  function int check(base b);
    box #(7) b7;
    box #(4) b4;
    box4 t4;
    box d;
    int r;
    r = $cast(b7, b) * 1000 + $cast(b4, b) * 100 + $cast(t4, b) * 10
        + $cast(d, b);
    if (b7 != null || d != null || b4 == null || t4 == null
        || b4.value() != 4)
      r = -1;
    return r;
  endfunction
  function void post_randomize();
    box #(4) made;
    made = new;
    results = check(made);
  endfunction
endclass

module top;
  base b;
  box #(4) b4;
  box #(7) b7;
  box d;
  probe p;
  int failures;
  int a7;
  int a4;
  int ad;
  initial begin
    b4 = new;
    b = b4;
    b4 = null;
    a7 = $cast(b7, b);
    a4 = $cast(b4, b);
    ad = $cast(d, b);
    if (a7 != 0 || a4 != 1 || ad != 0 || b7 != null || d != null
        || b4 == null) begin
      $display("FAIL module %0d %0d %0d", a7, a4, ad);
      failures++;
    end
    p = new;
    if (p.check(b) != 110) begin
      $display("FAIL method %0d", p.check(b));
      failures++;
    end
    if (!p.randomize() || p.results != 110) begin
      $display("FAIL executor %0d", p.results);
      failures++;
    end
    if (failures == 0) $display("PASS");
  end
endmodule
