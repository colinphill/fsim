// SPDX-License-Identifier: Apache-2.0
//
// Wide signal accesses in a behavioral thread (an initial block with delays),
// compiled inline by the v4 kernel: a constant written to a 128 bit variable
// (KOp::wide_const, wide_store), a read and a copy of it (wide_load,
// wide_copy), a nonblocking write of its complement (wide_alu unary_not,
// wide_store sub 1) and a part-select write of a 64 bit piece. No operation
// helper runs them: only the display (a library operation) does.
// IEEE 1800-2023 10.4.1: a blocking assignment takes effect at once; 10.4.2:
// a nonblocking one in the NBA region of its time step, so the display one
// time unit later sees it. 11.5.1: a constant part select writes only its
// bits.
//   a = 00112233445566778899aabbccddeeff at time 0, b = a at time 1,
//   c <= ~b at time 2 (visible at 3): ~b = ffeeddccbbaa99887766554433221100,
//   a[127:64] = 64'h0123456789abcdef at time 3: a = 0123456789abcdef8899aabbccddeeff
module top;
  reg [127:0] a;
  reg [127:0] b;
  reg [127:0] c;
  initial begin
    a = 128'h00112233445566778899aabbccddeeff;
    #1 b = a;
    #1 c <= ~b;
    #1 $display("inl thr a=%h b=%h c=%h", a, b, c);
    a[127:64] = 64'h0123456789abcdef;
    #1 $display("inl thr a=%h", a);
    $finish;
  end
endmodule
