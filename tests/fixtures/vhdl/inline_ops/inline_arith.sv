// SPDX-License-Identifier: Apache-2.0
//
// Four-state division, modulo and power (IEEE 1800-2023 11.4.3, Table 11-4)
// and dynamic part selects of a wide variable in both directions (11.5.1:
// bits outside the object read x and are not written), which the v4 kernel
// compiles inline. The expected lines (inline_arith.expected) are derived by
// hand in the comments.
module top;
  logic [127:0] w;
  logic [127:0] r;
  logic [15:0] ua, ub;
  logic signed [15:0] sa, sb;
  integer i, j;
  logic [3:0] p;

  initial begin
    // 1000 / 7 = 142, 1000 % 7 = 6; a zero divisor gives x.
    ua = 16'd1000; ub = 16'd7;
    $display("inl udiv=%0d umod=%0d", ua / ub, ua % ub);
    ub = 16'd0;
    $display("inl udiv0=%0d umod0=%0d", ua / ub, ua % ub);
    // -100 / 7 = -14, -100 % 7 = -2 (the sign of the first operand);
    // 100 / -7 = -14, 100 % -7 = 2.
    sa = -16'sd100; sb = 16'sd7;
    $display("inl sdiv=%0d smod=%0d", sa / sb, sa % sb);
    sa = 16'sd100; sb = -16'sd7;
    $display("inl sdiv2=%0d smod2=%0d", sa / sb, sa % sb);
    sb = 16'sd0;
    $display("inl sdiv0=%0d", sa / sb);
    // Table 11-4: 2 ** 10 = 1024; (-2) ** 3 = -8; a negative exponent gives
    // 1 for a base of 1, the parity of the exponent for -1, 0 for a base
    // of 2 and x for a base of 0.
    i = 2; j = 10; $display("inl pow1=%0d", i ** j);
    i = -2; j = 3; $display("inl pow2=%0d", i ** j);
    i = -1; j = -3; $display("inl pow3=%0d", i ** j);
    i = -1; j = -2; $display("inl pow4=%0d", i ** j);
    i = 1; j = -5; $display("inl pow5=%0d", i ** j);
    i = 2; j = -1; $display("inl pow6=%0d", i ** j);
    i = 0; j = -1; $display("inl pow7=%0d", i ** j);
    // 3 ** 4 = 81; 2 ** 16 wraps to 0 in 16 bits.
    ua = 16'd3; ub = 16'd4; $display("inl upow=%0d", ua ** ub);
    ua = 16'd2; ub = 16'd16; $display("inl upow2=%0d", ua ** ub);
    // w[127:96] = deadbeef, [95:64] = 01234567, [63:32] = 89abcdef,
    // [31:0] = fedcba98.
    w = {32'hdeadbeef, 32'h01234567, 32'h89abcdef, 32'hfedcba98};
    // Bits 39:32 are ef = 1110_1111: bits 39:36 are e, bits 36:33 are 0111.
    i = 36;
    p = w[i +: 4];
    $display("inl plus=%h", p);
    p = w[i -: 4];
    $display("inl minus=%h", p);
    // Bits 129:126 of a 128-bit object: bits 129 and 128 read x, bits 127
    // and 126 are the top two bits of d = 1101, both 1.
    i = 126;
    $display("inl outside=%b", w[i +: 4]);
    i = 'x;
    $display("inl unknown=%b", w[i +: 4]);
    // Writes: e -> a at bits 39:36 gives af in bits 39:32.
    r = w; i = 36; r[i +: 4] = 4'ha;
    $display("inl wplus=%h", r[63:32]);
    // Bits 36:33 = 0111 cleared: 1110_0001 = e1.
    r = w; r[i -: 4] = 4'h0;
    $display("inl wminus=%h", r[63:32]);
    // Only bits 127:126 of the part select [129:126] exist; they take the
    // two low bits of 0101 (bit 126 = 1, bit 127 = 0): d = 1101 -> 0101 = 5.
    r = w; i = 126; r[i +: 4] = 4'b0101;
    $display("inl wpartial=%h", r[127:96]);
    // An unknown base writes nothing.
    r = w; i = 'x; r[i +: 4] = 4'b0101;
    $display("inl wunknown=%h", r[127:96]);
    // A nonblocking write at bits 39:36.
    r = w; i = 36; r[i +: 4] <= 4'h5;
    #1 $display("inl nba=%h", r[63:32]);
    // A dynamic bit write: w[103:96] is ef = 1110_1111, whose bit 4 (bit 100
    // of w) is 0; setting it gives ff.
    r = w; i = 100; r[i] = 1'b1;
    $display("inl wbit=%h", r[103:96]);
    $finish;
  end
endmodule
