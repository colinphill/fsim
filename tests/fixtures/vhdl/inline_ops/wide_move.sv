// SPDX-License-Identifier: Apache-2.0
//
// Whole-value moves of a wide variable into a continuous assignment's net
// (the v4 kernel fuses a read, an optional dynamic part select and the write
// into one KOp::wide_move when it can store the target in place). The
// targets here the move does not take, a net with two drivers (a resolved
// slot, IEEE 1800-2023 6.6.1) and a part select that reverses the bits of an
// ascending source, compile as wide_load, wide_alu and wide_store instead,
// with no operation helper. Oracle, one bit at a time (11.5.1: bits outside
// the source read x, and so does a select with an unknown base):
//   w[256] descending [255:0], w[i +: 72]: result bit b is w[i + b];
//   a[0:255] ascending, a[i +: 72] is a[i : i + 71]: result bit b (counted
//   from the right) is a[i + 71 - b].
// A net with a driver of all z and one other takes the other's value (z
// loses to every value, 6.6.1), so n === w and nr === pa. The explicit lines
// are derived by hand: with
//   w = {0123456789abcdef, fedcba9876543210, 0011223344556677, 8899aabbccddeeff}
// (bits 255:192, 191:128, 127:64, 63:0), w[64 +: 72] is bits 135:64: the low
// byte of the second word (10), then the third word. With a = w (the leftmost
// bit of a takes the most significant bit of w, so a[k] is w[255 - k]),
// a[64 +: 72] = a[64:135] = w[191:120]: the second word, then the top byte of
// the third word (00): fedcba9876543210 00.
module top;
  logic [255:0] w;
  logic [0:255] a;
  integer i;
  wire [71:0] pd = w[i +: 72];
  wire [71:0] pm = w[i -: 72];
  wire [71:0] pa = a[i +: 72];
  wire [255:0] n;
  assign n = w;
  assign n = {256{1'bz}};
  wire [255:0] n2;
  assign n2[255:128] = w[127:0];
  assign n2[255:128] = {128{1'bz}};
  assign n2[127:0] = w[255:128];
  wire [71:0] nr;
  assign nr = a[i +: 72];
  assign nr = {72{1'bz}};
  integer checks = 0;
  integer bad = 0;
  integer b;
  logic [71:0] want_d, want_m, want_a;

  task automatic check(input integer base);
    begin
      i = base;
      for (b = 0; b < 72; b = b + 1) begin
        want_d[b] = (base + b >= 0 && base + b <= 255) ? w[base + b] : 1'bx;
        want_m[b] = (base - 71 + b >= 0 && base - 71 + b <= 255)
          ? w[base - 71 + b] : 1'bx;
        want_a[b] = (base + 71 - b >= 0 && base + 71 - b <= 255)
          ? a[base + 71 - b] : 1'bx;
      end
      if (base === 32'bx) begin
        want_d = {72{1'bx}};
        want_m = {72{1'bx}};
        want_a = {72{1'bx}};
      end
      #1;
      if (pd !== want_d) bad = bad + 1;
      if (pm !== want_m) bad = bad + 1;
      if (pa !== want_a) bad = bad + 1;
      if (nr !== want_a) bad = bad + 1;
      if (n !== w) bad = bad + 1;
      if (n2 !== {w[127:0], w[255:128]}) bad = bad + 1;
      checks = checks + 1;
    end
  endtask

  initial begin
    for (b = 0; b < 256; b = b + 1) begin
      w[b] = (b % 7 == 3) ? 1'bx : ((b * 3) % 7 < 3);
    end
    a = ~w;
    for (b = 0; b < 256; b = b + 1) begin
      if (b % 11 == 5) a[b] = 1'bz;
    end
    for (i = -80; i <= 280; i = i + 17) begin
      check(i);
    end
    check(0);
    check(184);
    check(185);
    check(-1);
    check(32'bx);
    w = {64'h0123456789abcdef, 64'hfedcba9876543210,
         64'h0011223344556677, 64'h8899aabbccddeeff};
    a = w;
    i = 64;
    #1 $display("inl move explicit=%h", pd);
    $display("inl move explicit_a=%h", pa);
    $display("inl move explicit_nr=%h", nr);
    $display("inl move whole=%0d", n === w);
    $display("inl move checks=%0d mismatches=%0d", checks, bad);
    $finish;
  end
endmodule
