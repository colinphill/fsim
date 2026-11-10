// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 10.7, 11.8.2: a value narrower than the class property it
// is assigned to extends by its own signedness, not by the property's. An
// unsigned 1'b1, bit or logic [3:0] value zero-extends into a signed int or
// byte property; a signed 4'sb1000 sign-extends. The assignments run in a
// class method the module calls, in post_randomize (the class method
// executor), from the module through a handle, and on a static property.
class C;
  rand bit x;
  int p1, p2, p3, p4;
  byte b1;
  static int s1;

  function void set();
    bit one;
    logic [3:0] s;
    one = 1;
    s = 4'b1000;
    p1 = 1'b1;
    p2 = one;
    p3 = 4'sb1000;
    p4 = s;
    b1 = 1'b1;
    s1 = s;
  endfunction

  function void post_randomize();
    set();
  endfunction
endclass

module top;
  C c;
  int failures;

  task automatic expect_values(string where);
    if (c.p1 != 1 || c.p2 != 1 || c.p3 != -8 || c.p4 != 8 || c.b1 != 1
        || C::s1 != 8) begin
      $display("FAIL %s %0d %0d %0d %0d %0d %0d", where, c.p1, c.p2, c.p3,
               c.p4, c.b1, C::s1);
      failures++;
    end
  endtask

  initial begin
    c = new;
    if (!c.randomize()) begin
      $display("FAIL randomize");
      failures++;
    end
    expect_values("post_randomize");
    c = new;
    C::s1 = 0;
    c.set();
    expect_values("call");
    c.p1 = 1'b1;
    c.p3 = 4'sb1000;
    C::s1 = 1'b1;
    if (c.p1 != 1 || c.p3 != -8 || C::s1 != 1) begin
      $display("FAIL module %0d %0d %0d", c.p1, c.p3, C::s1);
      failures++;
    end
    if (failures == 0) $display("PASS");
  end
endmodule
