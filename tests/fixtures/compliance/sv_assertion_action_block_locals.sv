// fsim: top=sv_assertion_action_block_locals std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 16.14.1, 9.3.1, 9.3.5: the pass and fail action blocks of
// a concurrent assertion are statements; a labelled or unnamed block in an
// action declares its own locals, and each action runs at every outcome
// the assertion reaches.
module sv_assertion_action_block_locals;
  logic clk = 0;
  logic a = 1;
  int passes;
  int failures_seen;
  int failures;

  always #1 clk = ~clk;

  assert property (@(posedge clk) a)
    begin : act
      int x;
      x = 1;
      passes += x;
    end
  else
    begin : act_failed
      int x;
      x = 1;
      failures_seen += x;
    end

  assert property (@(posedge clk) a)
    begin
      int y;
      y = 2;
      passes += y;
    end
  else
    begin
      int y;
      y = 2;
      failures_seen += y;
    end

  initial begin
    #4 a = 0;
    #2 a = 1;
    #4;
    if (passes == 0 || failures_seen == 0) begin
      $display("FAIL actions passes=%0d failures=%0d", passes, failures_seen);
      failures++;
    end
    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
