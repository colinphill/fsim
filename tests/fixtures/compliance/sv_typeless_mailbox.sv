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
// (10.9). An enumeration literal and a cast to an enumeration have the
// enumeration's type (6.19.3, 6.24.1), which no other package's
// enumeration of the same name shares; two specializations of one
// parameterized class are different types, two handles of one are the
// same (8.25, 6.22.1). A function call's message has the function's return
// type (13.4.1) and a method call's the type its class declares (8.10); a
// built-in method's has the type its clause gives: string methods (6.16),
// an enumeration's first/next of the enumeration and num of int (6.19.5),
// an array's size of int (7.5.1); a system function's has the type its
// clause gives: $urandom and $urandom_range int unsigned (18.13), $random
// integer (20.14.1), $time time and $realtime realtime (20.3), $sformatf
// and $typename string (21.3.3, 20.6.1), $cast int (6.24.2), $bits and the
// array queries integer (20.6.2, 20.7), $clog2 and $rtoi integer (20.8.1,
// 20.5), $stime logic [31:0] (20.3.2), $realtobits bit [63:0] and
// $shortrealtobits bit [31:0] (20.5), $countones and $countbits int and
// $onehot and $isunknown bit (20.9), $itor, $bitstoreal and $sqrt real
// (20.5, 20.8.2), the file and plusarg functions integer (21.3, 21.6);
// $signed and $unsigned have their operand's type with the sign changed
// (11.7) and $sampled its operand's (16.9.3).
`define CHECK(what, ok) \
  if (!(ok)) begin \
    $display("FAIL %s", what); \
    failures++; \
  end
package sv_typeless_mailbox_pkg;
  typedef struct packed { logic [3:0] hi; logic [3:0] lo; } shared_t;
endpackage

package sv_typeless_mailbox_other;
  typedef enum { P_RED, P_GREEN } color_t;
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
  class sized #(int W = 1);
    bit [W-1:0] value;
  endclass

  class maker;
    function packet make(int id);
      make = new(id);
    endfunction
    function color_t pick();
      return GREEN;
    endfunction
    function shortint narrow();
      return 3;
    endfunction
  endclass
  function int plain();
    return 4;
  endfunction
  function color_t chosen();
    return BLUE;
  endfunction
  function real half();
    return 2.5;
  endfunction

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

    // Enumeration literals and casts carry their enumeration.
    begin
      color_t color;
      sv_typeless_mailbox_other::color_t other;
      int got;
      box.put(GREEN);
      got = box.try_get(other);
      `CHECK("literal not another package's enumeration", got < 0)
      got = box.try_get(color);
      `CHECK("literal of the enumeration", got > 0 && color == GREEN)
      box.put(color_t'(2));
      got = box.try_get(color);
      `CHECK("cast to the enumeration", got > 0 && color == BLUE)
      box.put(sv_typeless_mailbox_other::P_GREEN);
      got = box.try_get(color);
      `CHECK("package literal not the module's enumeration", got < 0)
      got = box.try_get(other);
      `CHECK("package literal of its enumeration",
        got > 0 && other == sv_typeless_mailbox_other::P_GREEN)
    end

    // Specializations of one parameterized class.
    begin
      sized #(4) four = new;
      sized #(4) four_again;
      sized #(8) eight;
      int got;
      four.value = 4'hA;
      box.put(four);
      got = box.try_get(eight);
      `CHECK("other specialization", got < 0)
      got = box.try_get(four_again);
      `CHECK("same specialization", got > 0 && four_again.value == 4'hA)
    end

    // Call and method results.
    begin
      maker mk = new;
      color_t color;
      packet handle;
      shortint narrow;
      string text;
      byte code;
      integer number;
      real value;
      int got;
      int whole;
      int sizes[3];
      box.put(plain());
      got = box.try_peek(color);
      `CHECK("function int not an enumeration", got < 0)
      got = box.try_get(whole);
      `CHECK("function int", got > 0 && whole == 4)
      box.put(chosen());
      got = box.try_peek(whole);
      `CHECK("function enumeration not int", got < 0)
      got = box.try_get(color);
      `CHECK("function enumeration", got > 0 && color == BLUE)
      box.put(half());
      got = box.try_get(value);
      `CHECK("function real", got > 0 && value == 2.5)
      box.put(mk.make(9));
      got = box.try_get(handle);
      `CHECK("method class handle", got > 0 && handle.id == 9)
      box.put(mk.pick());
      got = box.try_get(color);
      `CHECK("method enumeration", got > 0 && color == GREEN)
      box.put(mk.narrow());
      got = box.try_peek(whole);
      `CHECK("method shortint not int", got < 0)
      got = box.try_get(narrow);
      `CHECK("method shortint", got > 0 && narrow == 3)
      text = "abc";
      box.put(text.len());
      got = box.try_peek(narrow);
      `CHECK("len not shortint", got < 0)
      got = box.try_get(whole);
      `CHECK("len int", got > 0 && whole == 3)
      box.put(text.substr(1, 2));
      got = box.try_get(text);
      `CHECK("substr string", got > 0 && text == "bc")
      box.put(text.getc(0));
      got = box.try_get(code);
      `CHECK("getc byte", got > 0 && code == "b")
      text = "12";
      box.put(text.atoi());
      got = box.try_peek(whole);
      `CHECK("atoi not int", got < 0)
      got = box.try_get(number);
      `CHECK("atoi integer", got > 0 && number == 12)
      color = RED;
      box.put(color.next());
      got = box.try_get(color);
      `CHECK("enumeration next", got > 0 && color == GREEN)
      box.put(color.num());
      got = box.try_get(whole);
      `CHECK("enumeration num", got > 0 && whole == 3)
      box.put(sizes.size());
      got = box.try_get(whole);
      `CHECK("array size", got > 0 && whole == 3)
      `CHECK("results all taken", box.num() == 0)
      begin
        int unsigned draw;
        time now;
        box.put($urandom);
        got = box.try_peek(whole);
        `CHECK("urandom not int", got < 0)
        got = box.try_get(draw);
        `CHECK("urandom int unsigned", got > 0)
        box.put($urandom_range(3, 1));
        got = box.try_get(draw);
        `CHECK("urandom_range int unsigned",
               got > 0 && draw >= 1 && draw <= 3)
        box.put($random);
        got = box.try_peek(whole);
        `CHECK("random not int", got < 0)
        got = box.try_get(number);
        `CHECK("random integer", got > 0)
        box.put($time);
        got = box.try_peek(number);
        `CHECK("time not integer", got < 0)
        got = box.try_get(now);
        `CHECK("time", got > 0 && now == $time)
        box.put($realtime);
        got = box.try_get(value);
        `CHECK("realtime", got > 0 && value == $realtime)
        box.put($sformatf("%0d", 7));
        got = box.try_get(text);
        `CHECK("sformatf string", got > 0 && text == "7")
        box.put($typename(whole));
        got = box.try_get(text);
        `CHECK("typename string", got > 0 && text == "int")
        box.put($cast(color, 1));
        got = box.try_get(whole);
        `CHECK("cast int", got > 0 && whole == 1 && color == GREEN)
        `CHECK("system results all taken", box.num() == 0)
      end
      begin
        integer four;
        bit one;
        logic [7:0] bits8;
        logic signed [7:0] sbits8;
        logic [7:0] probe = 8'h35;
        logic signed [7:0] negative = -8'sd3;
        integer fd;
        integer unsigned stamp;
        int unsigned two_state;
        bit [63:0] bits64;
        logic [63:0] logic64;
        box.put($bits(probe));
        got = box.try_peek(whole);
        `CHECK("bits not int", got < 0)
        got = box.try_get(four);
        `CHECK("bits integer", got > 0 && four == 8)
        box.put($countones(probe));
        got = box.try_peek(four);
        `CHECK("countones not integer", got < 0)
        got = box.try_get(whole);
        `CHECK("countones int", got > 0 && whole == 4)
        box.put($countbits(probe, '0));
        got = box.try_get(whole);
        `CHECK("countbits int", got > 0 && whole == 4)
        box.put($onehot(probe));
        got = box.try_get(one);
        `CHECK("onehot bit", got > 0 && one == 1'b0)
        box.put($isunknown(probe));
        got = box.try_get(one);
        `CHECK("isunknown bit", got > 0 && one == 1'b0)
        box.put($itor(5));
        got = box.try_get(value);
        `CHECK("itor real", got > 0 && value == 5.0)
        box.put($bitstoreal(64'h4004000000000000));
        got = box.try_get(value);
        `CHECK("bitstoreal real", got > 0 && value == 2.5)
        box.put($sqrt(16.0));
        got = box.try_get(value);
        `CHECK("sqrt real", got > 0 && value == 4.0)
        box.put($signed(probe));
        got = box.try_peek(bits8);
        `CHECK("signed not unsigned", got < 0)
        got = box.try_get(sbits8);
        `CHECK("signed of the operand", got > 0 && sbits8 == 8'sh35)
        box.put($unsigned(negative));
        got = box.try_peek(sbits8);
        `CHECK("unsigned not signed", got < 0)
        got = box.try_get(bits8);
        `CHECK("unsigned of the operand", got > 0 && bits8 == 8'hfd)
        box.put($sampled(probe));
        got = box.try_get(bits8);
        `CHECK("sampled of the operand", got > 0 && bits8 == 8'h35)
        box.put($left(probe));
        got = box.try_peek(whole);
        `CHECK("left not int", got < 0)
        got = box.try_get(four);
        `CHECK("left integer", got > 0 && four == 7)
        box.put($dimensions(probe));
        got = box.try_get(four);
        `CHECK("dimensions integer", got > 0 && four == 1)
        box.put($size(probe));
        got = box.try_get(four);
        `CHECK("size integer", got > 0 && four == 8)
        box.put($test$plusargs("sv_typeless_mailbox_absent"));
        got = box.try_peek(whole);
        `CHECK("test plusargs not int", got < 0)
        got = box.try_get(four);
        `CHECK("test plusargs integer", got > 0 && four == 0)
        fd = $fopen("sv_typeless_mailbox.txt", "w");
        box.put($fopen("sv_typeless_mailbox_more.txt", "w"));
        got = box.try_get(four);
        `CHECK("fopen integer", got > 0 && four != 0)
        $fclose(four);
        box.put($feof(fd));
        got = box.try_get(four);
        `CHECK("feof integer", got > 0)
        box.put($fgetc(fd));
        got = box.try_get(four);
        `CHECK("fgetc integer", got > 0)
        $fclose(fd);
        box.put($clog2(9));
        got = box.try_peek(whole);
        `CHECK("clog2 not int", got < 0)
        got = box.try_get(four);
        `CHECK("clog2 integer", got > 0 && four == 4)
        box.put($rtoi(-2.5));
        got = box.try_peek(whole);
        `CHECK("rtoi not int", got < 0)
        got = box.try_get(four);
        `CHECK("rtoi integer", got > 0 && four == -2)
        box.put($stime);
        got = box.try_peek(two_state);
        `CHECK("stime not int unsigned", got < 0)
        got = box.try_peek(four);
        `CHECK("stime not integer", got < 0)
        got = box.try_get(stamp);
        `CHECK("stime logic 32", got > 0 && stamp == $stime)
        box.put($realtobits(2.5));
        got = box.try_peek(logic64);
        `CHECK("realtobits not logic", got < 0)
        got = box.try_get(bits64);
        `CHECK("realtobits bit 64", got > 0 && bits64 == 64'h4004000000000000)
        `CHECK("more system results all taken", box.num() == 0)
      end
    end

    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
