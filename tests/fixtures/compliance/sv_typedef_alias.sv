// fsim: top=sv_typedef_alias std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.18, 6.22.1 a: a typedef that renames a type is that
// type. An unpacked structure assigns to and from its aliases, through a
// chain of aliases, a block's alias and a package's; an enumeration's
// alias has the enumeration's methods and literals; a typeless mailbox
// (15.4.9) takes a message of an alias's type into the renamed type.
`define CHECK(what, ok) \
  if (!(ok)) begin \
    $display("FAIL %s", what); \
    failures++; \
  end
package sv_typedef_alias_pkg;
  typedef struct { int a; } shape_t;
  typedef enum logic [1:0] { P0 = 1, P1 = 2 } level_t;
endpackage

module sv_typedef_alias;
  typedef struct { int a; } s_t;
  typedef s_t s2_t;
  typedef s2_t s3_t;
  typedef enum { RED, GREEN, BLUE } color_t;
  typedef color_t hue_t;
  typedef sv_typedef_alias_pkg::shape_t shape2_t;
  typedef sv_typedef_alias_pkg::level_t level2_t;
  int failures;

  initial begin
    s_t base;
    s3_t chained;
    sv_typedef_alias_pkg::shape_t shape;
    shape2_t shape_alias;
    hue_t hue;
    color_t color;
    level2_t level;
    mailbox box = new;
    int got;

    base.a = 5;
    chained = base;
    `CHECK("structure to alias chain", chained.a == 5)
    chained.a = 6;
    base = chained;
    `CHECK("alias chain to structure", base.a == 6)
    begin
      typedef s3_t s4_t;
      s4_t local_alias;
      local_alias = base;
      `CHECK("block alias", local_alias.a == 6)
    end
    shape_alias.a = 7;
    shape = shape_alias;
    `CHECK("package structure alias", shape.a == 7)

    hue = GREEN;
    `CHECK("enumeration alias name", hue.name() == "GREEN")
    `CHECK("enumeration alias next", hue.next() == BLUE)
    color = hue;
    `CHECK("enumeration alias to enumeration", color == GREEN)
    level = sv_typedef_alias_pkg::P1;
    `CHECK("package enumeration alias", level.name() == "P1"
      && level.first() == sv_typedef_alias_pkg::P0)

    box.put(hue);
    got = box.try_get(color);
    `CHECK("mailbox alias message", got > 0 && color == GREEN)
    box.put(color_t'(2));
    got = box.try_get(hue);
    `CHECK("mailbox message into alias", got > 0 && hue == BLUE)

    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
