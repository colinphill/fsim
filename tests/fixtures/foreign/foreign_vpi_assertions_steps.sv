// SPDX-License-Identifier: Apache-2.0
// Assertion step callbacks (IEEE 1800-2023 39.4.2 cbAssertionStepSuccess and
// cbAssertionStepFailure, 39.5.2 vpiAssertionEnableStep and
// vpiAssertionDisableStep).
//
// clk rises at 5, 15, 25 and 35. a is 0 until 10, 1 from 10 to 20 and 0 from
// 20; b is 1 from 20 to 30 and 0 otherwise. The sequence `a ##1 b` compiles to
// check(a), delay 1, check(b), match: the origin is state 0, the point that
// waits for b is state 4 (2 + program counter 2) and the match is state 1.
// An attempt starts at every rising edge and checks a at its own edge:
//   start 5:  a sampled 0, the first check fails (step 0 -> 4, a failure).
//   start 15: a sampled 1, the first check passes (step 0 -> 4); b sampled 1
//             at 25, so the second check would pass (step 4 -> 1).
//   start 25: a sampled 0, the first check fails.
//   start 35: a sampled 0, the first check fails.
// The library enables stepping at time 0 for the attempts starting at 5, 15,
// 25 and 35, then disables it for 25 at once (it never steps) and tries to
// disable it for 15 at time 20, while that attempt runs. The stepping mode of
// an attempt cannot change after it has started (39.5.2), so that control is
// refused (vpi_chk_error names it) and the attempt of 15 keeps stepping: its
// second check, b sampled 1 at 25, passes (step 4 -> 1). Each step names the
// boolean it checked as its matched expression (39.4.2 matched_exprs): a for
// the first check, b for the second.
module top;
  logic clk = 0, a = 0, b = 0;
  always #5 clk = ~clk;
  initial begin
    #10 a = 1;
    #10 begin a = 0; b = 1; end
    #10 b = 0;
    #12 $finish;
  end

  p_step: assert property (@(posedge clk) a ##1 b);
endmodule
