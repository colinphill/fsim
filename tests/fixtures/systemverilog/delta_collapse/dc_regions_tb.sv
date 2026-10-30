// SPDX-License-Identifier: Apache-2.0
module dc_regions_tb;
  logic clk = 1'b0;
  logic a = 1'b0;
  logic b = 1'b0;
  wire q, r;
  dc_regions dut(.clk(clk), .a(a), .b(b), .q(q), .r(r));
  always #5 clk = ~clk;
  // a changes in the active region of every step, b in the NBA region of
  // every other one: in those steps both inputs change in one time step.
  initial for (int step = 0; step < 6; step++) begin
    #10;
    a = ~a;
    if (step[0]) b <= ~b;
  end
  initial #80 $finish;
  always @(posedge clk) $display("%0t q=%b r=%b", $time, q, r);
endmodule
