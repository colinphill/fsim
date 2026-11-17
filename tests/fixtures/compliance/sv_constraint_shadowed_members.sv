// fsim: top=sv_constraint_shadowed_members std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 8.14, 8.15, 18.5, 18.7: a derived class's rand property
// hides a base class property of the same name only in the derived class's
// own code. A constraint declared in the base class constrains the base
// class's property (`x`, `this.x`), one in the derived class its own
// (`super.x` names the base's). A with-clause resolves names in the class
// of the handle randomize() is called through: `x` through a derived handle
// is the derived property, through a base handle the base one, and a name
// only the derived class declares is a calling-scope name through a base
// handle; `with (names)` binds only the listed names to the object. A path
// through a rand handle property (18.5.9) names the member visible in the
// handle property's class; so does randomize() called in a method, through
// `this`, or through an element of a handle array property, and `a.size`
// without parentheses is the array's size (7.10.2.1).
`define CHECK(what, ok) \
  if (!(ok)) begin \
    $display("FAIL %s", what); \
    failures++; \
  end
// A class of the compilation unit, randomized with paren-less size.
class unit_packet;
  rand int unsigned length;
  rand byte data[];
  constraint short_length { length inside {[1:4]}; }
endclass

module sv_constraint_shadowed_members;
  class base;
    rand bit [3:0] x;
    constraint in_base { x == 1; }
  endclass
  class derived extends base;
    rand bit [3:0] x;
    rand bit [3:0] y;
    constraint in_derived { x == 2; super.x < 4; }
  endclass
  class plain_base;
    rand bit [3:0] x;
  endclass
  class plain_derived extends plain_base;
    rand bit [3:0] x;
    rand bit [3:0] y;
  endclass

  class leaf_base;
    rand bit [3:0] m;
  endclass
  class leaf_derived extends leaf_base;
    rand bit [3:0] m;
  endclass
  class holder;
    rand leaf_base h;
    rand bit [3:0] k;
    rand leaf_base pair[2];
    function new();
      leaf_derived made = new;
      h = made;
      pair[0] = new;
      pair[1] = new;
    endfunction
    function int own_with();
      return randomize() with { k == 11; };
    endfunction
    function int this_with();
      return this.randomize() with { k == 12; };
    endfunction
  endclass

  derived d = new;
  holder c = new;
  leaf_derived view;
  unit_packet packet = new;
  base b;
  plain_derived pd = new;
  plain_base pb;
  bit [3:0] y = 9;
  int ok;
  int failures;

  initial begin
    b = d;
    pb = pd;
    ok = d.randomize();
    `CHECK("declared constraints", ok == 1 && d.x == 2 && b.x == 1)
    ok = pd.randomize() with { x == 7; this.y == 3; };
    `CHECK("with-clause through the derived handle", ok == 1 && pd.x == 7
      && pd.y == 3)
    ok = pb.randomize() with { x == 5; };
    `CHECK("with-clause through the base handle", ok == 1 && pb.x == 5)
    ok = pb.randomize() with { x == y; };
    `CHECK("derived-only name is a calling-scope name", ok == 1 && pb.x == 9)
    ok = pd.randomize() with (x) { x == y; };
    `CHECK("with (names): an unlisted name is a calling-scope name",
      ok == 1 && pd.x == 9)
    ok = c.randomize() with { h.m == 6; k == h.m + 1; };
    `CHECK("through a handle property", ok == 1 && c.h.m == 6 && c.k == 7)
    ok = c.randomize() with { this.h.m == 3; };
    `CHECK("through this and a handle property", ok == 1 && c.h.m == 3)
    ok = c.own_with();
    `CHECK("randomize() with in a method", ok == 1 && c.k == 11)
    ok = c.this_with();
    `CHECK("this.randomize() with in a method", ok == 1 && c.k == 12)
    ok = c.pair[1].randomize() with { m == 13; };
    `CHECK("an element of a handle array property", ok == 1
      && c.pair[1].m == 13)
    ok = packet.randomize() with { data.size == length; };
    `CHECK("paren-less size", ok == 1 && packet.data.size() == packet.length)
    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
