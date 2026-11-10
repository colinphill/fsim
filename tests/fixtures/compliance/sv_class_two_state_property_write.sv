// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.3.2.1, 6.11.2, 10.7: assigning a value with X or Z bits
// to a 2-state class property stores 0 for each of them; a 4-state property
// keeps them. Checked for int, bit vector, byte, a typedef of bit and a
// static property, written in post_randomize (the class method executor),
// in a method the module calls, and from the module.
typedef bit [3:0] bnib;
typedef logic [3:0] nib;

class P;
  rand bit [1:0] x;
  int i;
  int k;
  bit [3:0] b;
  byte y;
  bnib bn;
  nib l;
  static int s;

  function void write();
    i = 'x;
    b = 4'bz1x0;
    y = 8'bxxzz1100;
    bn = 'z;
    l = 4'bx1z0;
    k = l;
    s = 'x;
  endfunction

  function void post_randomize();
    write();
  endfunction
endclass

module top;
  P p;
  int failures;

  task automatic expect_written(string where);
    if (p.i !== 0 || p.b !== 4'b0100 || p.y !== 8'b00001100
        || p.bn !== 4'b0000 || p.l !== 4'bx1z0 || p.k !== 4
        || P::s !== 0) begin
      $display("FAIL %s %b %b %b %b %b %0d %b", where, p.i, p.b, p.y, p.bn,
               p.l, p.k, P::s);
      failures++;
    end
  endtask

  initial begin
    p = new;
    if (!p.randomize()) begin
      $display("FAIL randomize");
      failures++;
    end
    expect_written("post_randomize");
    p = new;
    P::s = 7;
    p.write();
    expect_written("call");
    p = new;
    P::s = 7;
    p.i = 'x;
    p.b = 4'bz1x0;
    p.y = 8'bxxzz1100;
    p.bn = 'z;
    p.l = 4'bx1z0;
    p.k = p.l;
    P::s = 'x;
    expect_written("module");
    if (failures == 0) $display("PASS");
  end
endmodule
