// SPDX-License-Identifier: Apache-2.0
//
// Fixed-size arrays far beyond the dense storage budget, which keep only
// their written elements (every other element holds the background element:
// the type's initial value, or the initializer's `default:`), reduced and
// compared as whole values in compiled member code of the v4 kernel. Every
// element takes part (IEEE 1800-2023 7.4, 7.12.3, 7.12.4, 11.4.5).
// N = 2^23 = 8388608 elements each.
// big[3] = 5, big[10] = -2, big[N-1] = 9, every other element 0:
//   sum 12; product 0 (an unstored 0); and 0; or 5 | -2 | 9 = -1;
//   xor 5 ^ -2 ^ 9 = -14 (the N - 3 zeros change nothing);
//   sum with (item * 2) = 10 - 4 + 18 = 24;
//   sum with (item + 1) = 6 - 1 + 10 + (N - 3) * 1 = 8388620;
//   product with (item + 3) = 8 * 1 * 12 * 3^(N-3) mod 2^32 = 1550960416;
//   sum with (item * item.index) = 5*3 - 2*10 + 9*(N-1) = 75497458.
// a module variable filled = '{default: 7}, then filled[4] = 1:
//   sum (N-1)*7 + 1 = 58720250; product 7^(N-1) mod 2^32 = 1792765367;
//   and 7 & 1 = 1; or 7 | 1 = 7; xor: N - 1 (odd) sevens give 7, ^ 1 = 6.
// logic [7:0] lb (background xxxxxxxx), lb[5] = 8'h12:
//   sum xxxxxxxx (an unknown operand); and 8'h12 & x per bit = 000x00x0;
//   or 8'h12 | x per bit = xxx1xx1x; xor xxxxxxxx.
// Equality (11.4.5): other = big: == 1; after other[7] = 1: != 1, == 0.
//   lb2 = lb: === 1, == x (the unknown backgrounds); after lb2[6] = 0:
//   === 0, == x (0 against x is unknown; no known bits differ).
// bits: bit b[N] with b[200*i] = 1 for i = 0..40000 (40001 ones, enough
// for the dense bit-plane image): or 1, and 0, xor 1 (an odd count), sum 1
// (one bit, mod 2). c = b: == 1; after c[1] = 1: == 0.
module top;
  localparam int N = 1 << 23;
  int filled [0:N-1] = '{default: 7};
  task automatic run();
    int big [0:N-1];
    int other [0:N-1];
    logic [7:0] lb [0:N-1];
    logic [7:0] lb2 [0:N-1];
    bit b [0:N-1];
    bit c [0:N-1];
    big[3] = 5;
    big[10] = -2;
    big[N-1] = 9;
    $display("inl big sum %0d product %0d and %0d or %0d xor %0d", big.sum(), big.product(),
      big.and(), big.or(), big.xor());
    $display("inl big with %0d %0d %0d %0d", big.sum() with (item * 2),
      big.sum() with (item + 1), big.product() with (item + 3),
      big.sum() with (item * item.index));
    filled[4] = 1;
    $display("inl filled sum %0d product %0d and %0d or %0d xor %0d", filled.sum(),
      filled.product(), filled.and(), filled.or(), filled.xor());
    lb[5] = 8'h12;
    $display("inl lb sum %b and %b or %b xor %b", lb.sum(), lb.and(), lb.or(), lb.xor());
    other = big;
    $display("inl big equal %0d", other == big);
    other[7] = 1;
    $display("inl big changed %0d %0d", other != big, other == big);
    lb2 = lb;
    $display("inl lb equal %b %b", lb2 === lb, lb2 == lb);
    lb2[6] = 8'h00;
    $display("inl lb changed %b %b", lb2 === lb, lb2 == lb);
    for (int i = 0; i <= 40000; i++) b[200 * i] = 1'b1;
    $display("inl bits or %b and %b xor %b sum %b", b.or(), b.and(), b.xor(), b.sum());
    c = b;
    $display("inl bits equal %b", c == b);
    c[1] = 1'b1;
    $display("inl bits changed %b", c == b);
  endtask
  initial run();
endmodule
