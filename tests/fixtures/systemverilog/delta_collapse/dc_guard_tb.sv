// SPDX-License-Identifier: Apache-2.0
module dc_guard_tb;
  logic clk = 1'b0;
  logic [5:0] a = '0;
  wire [7:0] q;
  dc_guard dut(.clk(clk), .a(a), .q(q));
  always #5 clk = ~clk;
  initial begin
    repeat (5) #7 a = a + 6'd13;
    #7 a = 6'bxxxxxx;
    #7 a = 6'b10x101;
    #7 a = 6'd9;
    #7 a = 6'd22;
    #7 a = 6'bz00000;
    repeat (4) #7 a = {a[4:0], 1'b1};
  end
  initial #160 $finish;
  always @(posedge clk) $display("%0t q=%h", $time, q);
endmodule
