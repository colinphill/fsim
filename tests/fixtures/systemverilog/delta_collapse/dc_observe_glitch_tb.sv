// SPDX-License-Identifier: Apache-2.0
module dc_observe_glitch_tb;
  logic clk = 1'b0;
  logic a = 1'b0;
  wire q, r;
  int events = 0;
  dc_observe_glitch dut(.clk(clk), .a(a), .q(q), .r(r));
  always #5 clk = ~clk;
  always @(q) events++;
  always @(posedge clk) $display("%0t r=%b", $time, r);
  initial begin
    repeat (6) #10 a = ~a;
    #10 $display("events=%0d", events);
    $finish;
  end
endmodule
