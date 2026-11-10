// SPDX-License-Identifier: Apache-2.0
// Attempt reporting of concurrent assertions (IEEE 1800-2023 39.4.2).
// clk rises at 5, 15, 25, 35, 45 and clk2 at 7, 17, 27, 37, 47; a and b stay
// 1, so an attempt of `a ##2 b` reaches its match two clock edges after it
// starts. rst is 1 from 30 to 40 and $finish runs at 52.
//
// p_dis (weak sequence in assert property, 16.12.2): attempts start at 5, 15,
// 25, 35 and 45. The attempt of 5 matches at 25. The disable condition reads
// current values and disables every attempt in progress when it becomes true
// (16.12), so when rst rises at 30 the attempts of 15 and 25 reach the
// disabled state at 30, between the clock edges: one
// cbAssertionDisabledEvaluation for each, its cb_time the time the attempt
// reached the disabled state (39.4.2), carrying that attempt's own start. At
// the edge of 35 rst is still 1: the attempt that starts there is disabled at
// its start (39.4.2). The attempt of 45 is running at the end of simulation
// and, being weak, does not fail.
//
// p_eos (strong sequence, 16.12.2): attempts start at 7, 17, 27, 37 and 47 and
// match at 27, 37 and 47. The attempts of 37 and 47 are running when the
// simulation ends at 52 and fail, each reported with its own start rather
// than the end time.
module top;
  logic clk = 0, clk2 = 0, a = 1, b = 1, rst = 0;
  always #5 clk = ~clk;
  initial begin
    #2;
    forever #5 clk2 = ~clk2;
  end

  p_dis: assert property (@(posedge clk) disable iff (rst) a ##2 b);
  p_eos: assert property (@(posedge clk2) strong(a ##2 b));

  initial begin
    #30 rst = 1;
    #10 rst = 0;
    #12 $finish;
  end
endmodule
