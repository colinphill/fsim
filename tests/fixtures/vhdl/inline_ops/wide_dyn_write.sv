// SPDX-License-Identifier: Apache-2.0
//
// Writes of a 128 bit value through a dynamic index or part select, blocking
// and nonblocking, which the v4 kernel compiles as writes of two 64 bit
// pieces (no operation helper). IEEE 1800-2023 11.5.1: the bits of a part
// select that lie outside the object are not written, and a write through an
// unknown index has no effect; 10.4.2: a nonblocking write takes effect after
// the active events of the time step, so the displays (one time unit after
// the edge) see both forms alike.
//   A = 00112233_44556677_8899aabb_ccddeeff   B = ~A = ffeeddcc_bbaa9988_77665544_33221100
//   v is 256 bits, v[p +: 128] = value; pm is {pm[1], pm[0]} of two 128 bit
//   elements. Words of v, w3 (bits 255:192) .. w0:
//   1 p=128, pm[1] = A:  v = 0011223344556677 8899aabbccddeeff 0 0;  pm = A:0
//   2 p=0,   pm[0] = B:  w1 w0 = B = ffeeddccbbaa9988 7766554433221100; pm = A:B
//   3 p=192, pm[2] = A:  bits 192..255 <- A[63:0] (the rest of the part lies
//                        above bit 255), so w3 = 8899aabbccddeeff; pm[2] is
//                        outside the array, pm unchanged
//   4 p=256, pm[x] = B:  both out of range or unknown: nothing changes
//   5 p=x,   pm[1] = B:  v unchanged; pm = B:B
//   6 p=64,  pm[0] = A:  bits 64..191 <- A: w1 = A[63:0] = 8899aabbccddeeff,
//                        w2 = A[127:64] = 0011223344556677; pm = B:A
//   7 p=-64, pm[1] = A:  bits -64..63 <- A, the part inside (bits 0..63) takes
//                        A[127:64]: w0 = 0011223344556677; pm = A:A
module top;
  reg clk = 0;
  reg [255:0] v_b = 256'h0;
  reg [255:0] v_n = 256'h0;
  reg [1:0][127:0] pm_b = 256'h0;
  reg [1:0][127:0] pm_n = 256'h0;
  reg [127:0] a;
  integer pos;
  integer idx;

  always @(posedge clk) begin
    v_b[pos +: 128] = a;
    v_n[pos +: 128] <= a;
    pm_b[idx] = a;
    pm_n[idx] <= a;
  end

  task automatic step(input integer n, input integer p, input integer i,
    input [127:0] value);
    begin
      pos = p;
      idx = i;
      a = value;
      #1 clk = 1;
      #1 clk = 0;
      #1 $display("inl dyn %0d vb=%h vn=%h pb=%h pn=%h", n, v_b, v_n, pm_b, pm_n);
    end
  endtask

  initial begin
    step(1, 128, 1, 128'h00112233445566778899aabbccddeeff);
    step(2, 0, 0, 128'hffeeddccbbaa99887766554433221100);
    step(3, 192, 2, 128'h00112233445566778899aabbccddeeff);
    step(4, 256, 32'bx, 128'hffeeddccbbaa99887766554433221100);
    step(5, 32'bx, 1, 128'hffeeddccbbaa99887766554433221100);
    step(6, 64, 0, 128'h00112233445566778899aabbccddeeff);
    step(7, -64, 1, 128'h00112233445566778899aabbccddeeff);
    $finish;
  end
endmodule
