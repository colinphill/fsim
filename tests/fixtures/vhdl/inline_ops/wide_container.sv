// SPDX-License-Identifier: Apache-2.0
//
// Reads and writes of the 128 bit elements of an unpacked array, blocking and
// nonblocking, in a four-state and a two-state array, which the v4 kernel
// compiles as loops over the element's words (KOp::mem_read and mem_write
// sub 1), with no operation helper. IEEE 1800-2023 7.4.6: a read of an
// element outside the array, or through an index with an unknown bit, gives
// the default of the element type (x in every bit for a four-state element,
// 0 for a two-state one); a write to such an element has no effect;
// 7.9.1: an element never written is x (four-state) or 0 (two-state);
// 10.4.2: the nonblocking write takes effect after the active events.
//   A = 00112233445566778899aabbccddeeff   B = ffeeddccbbaa99887766554433221100
//   step  write slot, value   read slot   rb = rn            rt
//   1     1, A                1           A                   A
//   2     3, B                3           B                   B
//   3     4 (outside), A      2 (unwritten) x...x             0
//   4     x, B                5 (outside)   x...x             0
//   5     0, A                x             x...x             0
// At the end mem[0..3] are A, A, unwritten, B (rb) and A, A, 0, B (rt).
module top;
  reg clk = 0;
  reg [127:0] mem_b [0:3];
  reg [127:0] mem_n [0:3];
  bit [127:0] mem_t [0:3];
  reg [127:0] rb, rn;
  bit [127:0] rt;
  reg [127:0] data;
  integer wslot;
  integer rslot;

  always @(posedge clk) begin
    mem_b[wslot] = data;
    mem_n[wslot] <= data;
    mem_t[wslot] = data;
  end

  always @(negedge clk) begin
    rb = mem_b[rslot];
    rn = mem_n[rslot];
    rt = mem_t[rslot];
  end

  task automatic step(input integer n, input integer w, input integer r,
    input [127:0] value);
    begin
      wslot = w;
      rslot = r;
      data = value;
      #1 clk = 1;
      #1 clk = 0;
      #1 $display("inl cont %0d rb=%h rn=%h rt=%h", n, rb, rn, rt);
    end
  endtask

  initial begin
    step(1, 1, 1, 128'h00112233445566778899aabbccddeeff);
    step(2, 3, 3, 128'hffeeddccbbaa99887766554433221100);
    step(3, 4, 2, 128'h00112233445566778899aabbccddeeff);
    step(4, 32'bx, 5, 128'hffeeddccbbaa99887766554433221100);
    step(5, 0, 32'bx, 128'h00112233445566778899aabbccddeeff);
    $display("inl cont final b=%h_%h_%h_%h t=%h_%h_%h_%h",
      mem_b[0], mem_b[1], mem_b[2], mem_b[3], mem_t[0], mem_t[1], mem_t[2], mem_t[3]);
    $finish;
  end
endmodule
