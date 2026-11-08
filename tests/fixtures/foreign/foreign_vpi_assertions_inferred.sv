// SPDX-License-Identifier: Apache-2.0
// Inferred clocks of concurrent assertions (IEEE 1800-2023 16.14.6, 37.50):
// p_def has no clock of its own and takes the default clocking's event
// (negedge clk); p_proc sits in an always procedure and takes its event
// control (posedge clk); p_own names its clock.
module top;
  logic clk = 0, a = 0, b = 0;
  always #5 clk = ~clk;
  default clocking cb @(negedge clk); endclocking

  p_def: assert property (a |-> b);
  always @(posedge clk) begin
    p_proc: assert property (a |-> b);
  end
  p_own: assert property (@(posedge clk) a |-> b);

  initial #20 $finish;
endmodule
