// SPDX-License-Identifier: Apache-2.0
//
// Dynamic part-select writes to the elements of an unpacked array (byte
// lanes of a 32 bit word memory), blocking and nonblocking, which the v4
// kernel compiles inline (KOp::mem_write with a part). IEEE 1800-2023 11.5.1:
// a write through an unknown index has no effect; 7.4.6: neither has a write
// to an element outside the array; 10.4.2: the nonblocking writes of one
// time step take effect in order, each into the element as the earlier ones
// left it. Each edge writes lane `lane` of mem[slot] with byte_in and the
// neighbouring lane (lane ^ 1) with ~byte_in. The expected lines
// (mem_part.expected) are derived by hand; the blocking and nonblocking
// memories always agree after the edge.
//
//   start        11111111_22222222_33333333_44444444
//   1 slot 1, lane 0, ab: mem[1][7:0] = ab, mem[1][15:8] = ~ab = 54
//                -> 222254ab
//   2 slot 1, lane 3, cd: mem[1][31:24] = cd, mem[1][23:16] = ~cd = 32
//                -> cd3254ab
//   3 slot 2, lane 2, ef: mem[2][23:16] = ef, mem[2][31:24] = ~ef = 10
//                -> 10ef3333
//   4 lane 2'bx1 (an unknown index), 5 slot 5 (outside 0:3), 6 slot 3'bx01:
//                no write, nothing changes
//   7 slot 3, lane 1, byte 8'hxa: mem[3][15:8] = xa, mem[3][7:0] = ~xa = x5
//                -> 4444xax5
//   8 slot 0, lane 3, 00: mem[0][31:24] = 00, mem[0][23:16] = ~00 = ff
//                -> 00ff1111
module top;
  reg clk = 0;
  reg [31:0] mem_b [0:3];
  reg [31:0] mem_n [0:3];
  reg [2:0] slot;
  reg [1:0] lane;
  reg [7:0] byte_in;
  integer k;

  always @(posedge clk) begin
    mem_b[slot][8 * lane +: 8] = byte_in;
    mem_b[slot][8 * (lane ^ 2'd1) +: 8] = ~byte_in;
    mem_n[slot][8 * lane +: 8] <= byte_in;
    mem_n[slot][8 * (lane ^ 2'd1) +: 8] <= ~byte_in;
  end

  task automatic step(input integer n, input [2:0] s, input [1:0] l, input [7:0] b);
    begin
      slot = s;
      lane = l;
      byte_in = b;
      #1 clk = 1;
      #1 clk = 0;
      #1 $display("inl part %0d b=%h_%h_%h_%h n=%h_%h_%h_%h", n,
        mem_b[0], mem_b[1], mem_b[2], mem_b[3],
        mem_n[0], mem_n[1], mem_n[2], mem_n[3]);
    end
  endtask

  initial begin
    for (k = 0; k < 4; k = k + 1) begin
      mem_b[k] = 32'h11111111 * (k + 1);
      mem_n[k] = 32'h11111111 * (k + 1);
    end
    step(1, 3'd1, 2'd0, 8'hab);
    step(2, 3'd1, 2'd3, 8'hcd);
    step(3, 3'd2, 2'd2, 8'hef);
    step(4, 3'd0, 2'bx1, 8'h55);
    step(5, 3'd5, 2'd1, 8'h66);
    step(6, 3'bx01, 2'd1, 8'h77);
    step(7, 3'd3, 2'd1, 8'hxa);
    step(8, 3'd0, 2'd3, 8'h00);
    $finish;
  end
endmodule
