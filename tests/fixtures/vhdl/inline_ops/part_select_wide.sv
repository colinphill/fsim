// SPDX-License-Identifier: Apache-2.0
//
// Dynamic part selects with a wide result (72 bits) of a 256 bit variable,
// which the v4 kernel compiles inline (KOp::wide_alu, dynamic_part_select),
// in both directions of the source's declared range. IEEE 1800-2023 11.5.1:
// in a part select, bits that are outside the object read x, and a select
// with an unknown base reads x. The oracle selects one bit at a time:
//   w[256] descending [255:0], w[i +: 72]: result bit b is w[i + b];
//   w[i -: 72]:                           result bit b is w[i - 71 + b];
//   a[0:255] ascending, a[i +: 72] is a[i : i + 71]: result bit b (counted
//   from the right) is a[i + 71 - b].
// Bases below, at, past the ends and unknown are checked (27 bases). The
// explicit line is derived by hand: with
//   w = {0123456789abcdef, fedcba9876543210, 0011223344556677, 8899aabbccddeeff}
// (bits 255:192, 191:128, 127:64, 63:0), w[64 +: 72] is bits 135:64, that is
// the low byte of the second word (10), then the third word.
module top;
  logic [255:0] w;
  logic [0:255] a;
  integer i;
  reg [71:0] k = 72'h0;
  wire [71:0] rd = w[i +: 72] ^ k;
  wire [71:0] rm = w[i -: 72] ^ k;
  wire [71:0] ra = a[i +: 72] ^ k;
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
      if (rd !== want_d) bad = bad + 1;
      if (rm !== want_m) bad = bad + 1;
      if (ra !== want_a) bad = bad + 1;
      checks = checks + 1;
    end
  endtask

  initial begin
    for (b = 0; b < 256; b = b + 1) begin
      w[b] = (b % 7 == 3) ? 1'bx : ((b * 3) % 7 < 3);
      a[b] = (b % 13 == 2) ? 1'bx : ((b * 5) % 9 < 4);
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
    i = 64;
    #1 $display("inl psel explicit=%h", rd);
    $display("inl psel checks=%0d mismatches=%0d", checks, bad);
    $finish;
  end
endmodule
