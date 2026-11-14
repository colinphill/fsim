// SPDX-License-Identifier: Apache-2.0
// Statements in post_randomize, which randomize() calls, behave as
// they do in a module: case inside with ranges and wildcard items (IEEE
// 1800-2023 12.5.4); bit-select, part-select, element and member select
// targets of packed variables in range, partly and wholly out of range and
// with an unknown index, and an X written to a 2-state bit (11.5.1, 7.2.1,
// 7.4.1); disable of a named block, of an enclosing block from a loop and of
// a loop body (9.6.2); $cast as a function to an enumeration, of a value
// that is not one of its members even after truncation, and to an
// integral variable (6.24.2); the enumeration methods, of a member and of a
// value that is not one (6.19.5); the string methods (6.16); and %d of a
// signed local (21.2.1.2, 6.11). The values are the LRM's.
typedef enum logic [1:0] {RED, GREEN, BLUE} color_t;
typedef enum logic [1:0] {DARK, LIGHT} shade_t;
typedef struct packed { logic [3:0] hi; logic [3:0] lo; } pair_t;
class item;
  rand bit [1:0] x;
  logic [7:0] b = 8'h00;
  logic [3:0] u = 4'b1x0z;
  logic [3:0][7:0] m = '0;
  pair_t p = '0;
  bit [3:0] two = 4'h0;
  bit [3:0] n = 4'h0;
  color_t color = RED;
  shade_t bad;
  string s;
  function void post_randomize();
    int k;
    logic [3:0] idx;
    for (int v = 0; v < 6; v++) begin
      case (v[3:0]) inside
        [0:1]: $display("inside %0d low", v);
        4'b001?: $display("inside %0d wildcard", v);
        5: $display("inside %0d five", v);
        default: $display("inside %0d default", v);
      endcase
    end
    case (u) inside
      4'b1?0?: $display("inside unknown first");
      default: $display("inside unknown default");
    endcase
    b[0] = 1'b1; b[7:6] = 2'b10; b[2+:2] = 2'b11; b[5-:1] = 1'b1;
    $display("select %b", b);
    b[9] = 1'b1; b[9:6] = 4'b0001; idx = 4'bx; b[idx] = 1'b0; b[idx+:2] = 2'b00;
    $display("range %b", b);
    m[1] = 8'hAB; m[2][3:0] = 4'h5; m[3][7] = 1'b1;
    $display("multi %h %h %h %h", m, m[1], m[2][3:0], m[3][7:4]);
    p.hi = 4'hC; p.lo[0] = 1'b1; p.lo[3:2] = 2'b10;
    $display("member %h %h %h %b", p, p.hi, p.lo, p.lo[2]);
    two[1] = 1'bx; two[2] = 1'b1; two[3] = 1'bz;
    $display("two %b", two);
    k = 0; k[4] = 1'b1; k[1:0] = 2'b11;
    $display("local %0d", k);
    begin : blk
      k = 1;
      disable blk;
      k = 2;
    end
    $display("disable %0d", k);
    begin : outer
      for (int i = 0; i < 4; i++) begin : inner
        if (i == 2) disable outer;
        k = i;
      end
      k = 50;
    end
    $display("disable outer %0d", k);
    for (int i = 0; i < 3; i++) begin : body
      if (i == 1) disable body;
      $display("body %0d", i);
    end
    k = $cast(color, 2);
    $display("cast %0d %s", k, color.name());
    k = $cast(color, 3);
    $display("cast %0d %s", k, color.name());
    k = $cast(color, 6);
    $display("cast %0d %s", k, color.name());
    k = $cast(n, 8'h1F);
    $display("cast %0d %0d", k, n);
    color = GREEN;
    color = color.next();
    $display("next %s", color.name());
    color = color.next();
    $display("wrap %s", color.name());
    color = color.prev(2);
    $display("prev %s", color.name());
    color = color.last();
    k = color.num();
    $display("last %s %0d %0d", color.name(), k, color.first());
    bad = shade_t'(3);
    $display("bad '%s' %b %b", bad.name(), bad.next(), bad.prev());
    s = "Hello";
    $display("str %0d %s %s %s %0d %0d %0d", s.len(), s.toupper(), s.tolower(), s.substr(1, 3), s.getc(0), s.compare("Hello"), s.compare("Help"));
    s.putc(0, "J");
    $display("putc %s", s);
    s = "42";
    $display("atoi %0d %0d %0d", s.atoi(), s.atohex(), s.atooct());
    s.itoa(-17);
    $display("itoa %s", s);
    s.hextoa(255);
    $display("hextoa %s %0d", s, s.icompare("FF"));
    k = -5;
    $display("signed %0d", k);
  endfunction
endclass

module top;
  item it = new;
  initial $display("result=%0d", it.randomize());
endmodule
