// fsim: top=sv_typeless_mailbox std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 15.4: a mailbox declared without a parameter is typeless
// (15.4.9) and carries messages of any type. get, peek, try_get and
// try_peek take the oldest message when its type is equivalent to the
// type of their variable (6.22.2): packed integral types of the same width,
// signedness and state count, real and realtime, the same enumeration or
// class. try_get and try_peek return a negative integer and leave the
// message when the types differ (15.4.6, 15.4.8). A waiting get receives
// a later put. A typed pattern's message has the type its name selects,
// a typedef of the module, an imported one or a package-qualified one
// (10.9).
`define CHECK(what, ok) \
  if (!(ok)) begin \
    $display("FAIL %s", what); \
    failures++; \
  end
package sv_typeless_mailbox_pkg;
  typedef struct packed { logic [3:0] hi; logic [3:0] lo; } shared_t;
endpackage

module sv_typeless_mailbox;
  import sv_typeless_mailbox_pkg::*;
  typedef enum { RED, GREEN, BLUE } color_t;
  typedef struct packed { logic [3:0] hi; logic [3:0] lo; } pair_t;
  class packet;
    int id;
    function new(int id);
      this.id = id;
    endfunction
  endclass

  mailbox box = new;
  mailbox bounded = new(1);
  int failures;

  initial begin
    int i;
    bit signed [31:0] same_as_int;
    byte b;
    logic [7:0] l8;
    reg [7:0] r8;
    string s;
    real r;
    realtime rt;
    color_t c;
    pair_t p;
    packet pk;
    int status;

    // A string literal is integral (11.10) and a sized literal is 4-state,
    // so the string and byte messages are put from variables.
    s = "text";
    b = 8'shA5;
    box.put(42);
    box.put(s);
    s = "";
    box.put(b);
    b = 0;
    box.put(3.5);
    box.put(BLUE);
    box.put(pair_t'{4'h1, 4'h2});
    pk = new(7);
    box.put(pk);
    `CHECK("num after puts", box.num() == 7)

    // The oldest message is an int; a string variable does not take it.
    status = box.try_get(s);
    `CHECK("try_get string of int is negative", status < 0)
    status = box.try_peek(b);
    `CHECK("try_peek byte of int is negative", status < 0)
    `CHECK("mismatch leaves the message", box.num() == 7)
    // bit signed [31:0] is equivalent to int.
    status = box.try_peek(same_as_int);
    `CHECK("try_peek equivalent type", status > 0 && same_as_int == 42)
    box.get(i);
    `CHECK("get int", i == 42)
    box.get(s);
    `CHECK("get string", s == "text")
    status = box.try_get(l8);
    `CHECK("try_get unsigned of signed byte message is negative", status < 0)
    box.get(b);
    `CHECK("get byte", b == 8'shA5)
    box.get(rt);
    `CHECK("get realtime of real message", rt == 3.5)
    status = box.try_get(i);
    `CHECK("try_get int of enum is negative", status < 0)
    box.peek(c);
    `CHECK("peek enum", c == BLUE)
    box.get(c);
    box.get(p);
    `CHECK("get packed struct", p.hi == 4'h1 && p.lo == 4'h2)
    pk = null;
    box.get(pk);
    `CHECK("get class handle", pk != null && pk.id == 7)
    `CHECK("empty", box.num() == 0)
    status = box.try_get(i);
    `CHECK("try_get empty is zero", status == 0)

    // logic [7:0] and reg [7:0] are the same type.
    bounded.put(8'hC3);
    status = bounded.try_put(8'h00);
    `CHECK("bounded mailbox full", status == 0)
    bounded.get(r8);
    `CHECK("get reg of logic message", r8 == 8'hC3)

    // A waiting get takes a later put.
    fork
      begin
        box.get(r);
      end
      begin
        #5 box.put(2.25);
      end
    join
    `CHECK("waiting get", r == 2.25 && $time == 5)

    // Patterns typed by a package's typedef, imported and qualified.
    begin
      shared_t shared;
      box.put(shared_t'{4'h5, 4'h6});
      box.get(shared);
      `CHECK("imported typed pattern message", shared == 8'h56)
      box.put(sv_typeless_mailbox_pkg::shared_t'{4'h7, 4'h8});
      box.get(shared);
      `CHECK("qualified typed pattern message", shared == 8'h78)
    end

    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
