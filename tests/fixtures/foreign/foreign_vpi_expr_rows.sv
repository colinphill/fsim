// SPDX-License-Identifier: Apache-2.0
// Expression and statement objects of the SystemVerilog VPI model (IEEE
// 1800-2023 37.59 details 3 and 5 casts, 11.4.2 increments, 11.4.14
// streaming, 37.18 struct members, 37.76 alias statements, 37.41 formals,
// 37.35 primitive terminals).
module top;
  typedef logic [3:0] nib_t;
  typedef struct packed { logic [3:0] hi; logic [3:0] lo; } pair_t;
  typedef enum logic [1:0] { A = 0, B = 1, C = 2 } e_t;
  typedef bit signed [7:0] sb_t;
  logic [7:0] a = 8'hC5;
  logic [7:0] ax = 8'b10x0_z101;
  int i = 5;
  int j;
  pair_t p = 8'h3C;
  struct { int x; logic [3:0] y; } u = '{7, 4'h9};
  logic [7:0] y8;
  logic [15:0] w16;
  logic [3:0] z4;
  e_t en;
  nib_t nb;
  sb_t sb;
  wire n1, n2, n3;
  alias n1 = n2 = n3;
  wire gw;
  and g (gw, a[0], a[1] & a[2]);
  function automatic int sum(input int v [4], input logic [3:0] m);
    sum = v[0] + m;
  endfunction
  initial begin : main
    nb = nib_t'(a);
    en = e_t'(2'd1);
    p = pair_t'(a);
    sb = sb_t'(ax);
    y8 = p.hi;
    z4 = u.y;
    j = i++;
    j = ++i;
    j = i--;
    j = --i;
    w16 = {>>{a}};
    w16 = {<<4{a}};
    w16 = {<<{a}};
    w16 = {<<8{a, ax}};
  end
endmodule
