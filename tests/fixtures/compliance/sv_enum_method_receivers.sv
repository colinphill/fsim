// fsim: top=sv_enum_method_receivers std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.19.5, 13.4.1: enumeration methods on every receiver form
// (a variable, a packed structure member, an unpacked array element and a
// class property), called with and without parentheses, and chained: the
// value next() or prev() returns has the receiver's enumeration type, so a
// further method applies to it, wrapping at the ends.
`define CHECK(what, ok) \
  if (!(ok)) begin \
    $display("FAIL %s", what); \
    failures++; \
  end
module sv_enum_method_receivers;
  typedef enum logic [2:0] { A = 1, B = 3, C = 6 } e_t;
  typedef struct packed { e_t m; } s_t;
  class holder;
    e_t p = C;
  endclass

  e_t e = B;
  s_t s;
  e_t items [2];
  holder h = new;
  int failures;

  initial begin
    s.m = C;
    items[1] = A;
    `CHECK("parenthesis-free", e.first == A && e.last == C && e.num == 3
      && e.next == C && e.prev == A && e.name == "B")
    `CHECK("called", e.first() == A && e.num() == 3 && e.next(2) == A
      && e.name() == "B")
    `CHECK("structure member", s.m.name == "C" && s.m.next == A)
    `CHECK("array element", items[1].name() == "A" && items[1].prev() == C)
    `CHECK("class property", h.p.name() == "C" && h.p.next(1) == A
      && h.p.next(2) == B)
    `CHECK("chained on a variable", e.next(1).next(1) == A
      && e.next(1).num() == 3)
    `CHECK("chained on a class property", h.p.next(1).next(1) == B
      && h.p.next().next() == B && h.p.prev().name() == "B")
    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
