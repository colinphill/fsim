// SPDX-License-Identifier: Apache-2.0
//
// Unpacked structures of a module, a structure variable and a fixed array
// of them, which the host bridges to packed signals (IEEE 1800-2023 7.2,
// 37.17): the v4 kernel binds their leaves from the signals' fields and
// writes them back as field stores, a continuous assignment reading one.
//   s = {8'h3c, {4'ha, 3'd5}, -7}: w follows s.a.
//   arr[i] = {i*16, {i, ?}, i*100}: arr[2].in.hi was never written (bit: 0).
//   arr[7] is out of range: logic leaves read x, bit and int ones 0 (7.4.6).
//   arr[3].in = s.in, arr[2] = s, then s = arr[1]: w follows to 8'h10.
//   Packed memories m1[2:0] and m2[5:7] compare as whole arrays (7.12):
//   equal by position, then m2[7] = 4 makes them differ.
module top;
  typedef struct { logic [3:0] lo; bit [2:0] hi; } inner_t;
  typedef struct { logic [7:0] a; inner_t in; int n; } outer_t;
  outer_t s;
  outer_t arr [1:3];
  inner_t t;
  outer_t u;
  int i;
  wire [7:0] w = s.a;
  logic [3:0] m1 [2:0];
  logic [3:0] m2 [5:7];
  initial begin
    s.a = 8'h3c; s.in.lo = 4'ha; s.in.hi = 3'd5; s.n = -7;
    #1 $display("inl box %h %h %0d %0d w=%h", s.a, s.in.lo, s.in.hi, s.n, w);
    for (i = 1; i <= 3; i++) begin
      arr[i].a = 8'(i * 16); arr[i].in.lo = 4'(i); arr[i].n = i * 100;
    end
    t = arr[2].in;
    $display("inl nested %h %0d", t.lo, t.hi);
    arr[3].in = s.in;
    $display("inl member copy %h %0d %0d", arr[3].in.lo, arr[3].in.hi, arr[3].n);
    i = 7;
    u = arr[i];
    $display("inl invalid %h %h %0d %0d", u.a, u.in.lo, u.in.hi, u.n);
    u = arr[1];
    $display("inl whole %h %h %0d", u.a, u.in.lo, u.n);
    arr[2] = s;
    $display("inl element %h %0d", arr[2].a, arr[2].n);
    s = u;
    #1 $display("inl assign %h %0d w=%h", s.a, s.n, w);
    m1[2] = 1; m1[1] = 2; m1[0] = 3;
    m2[5] = 1; m2[6] = 2; m2[7] = 3;
    $display("inl memories %b", m1 == m2);
    m2[7] = 4;
    $display("inl memories %b", m1 == m2);
  end
endmodule
