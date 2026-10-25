// fsim: top=multiclock_sequence_top std=2017
// SPDX-License-Identifier: Apache-2.0
// Multiclocked sequences (IEEE 1800-2017 16.13.1): `##1 @(c)` continues at
// the first tick of c strictly after the end of the left operand, `##0 @(c)`
// at its first tick at or after it.
module multiclock_sequence_top;
  bit fast;
  bit slow;
  bit same;
  bit high = 1'b1;
  bit low;
  int strict_passes;
  int strict_first = -1;
  int overlap_passes;
  int overlap_first = -1;
  int failures;
  int failure_first = -1;
  int handover_passes;
  int handover_first = -1;
  logic out0;
  logic out1;

  // fast rises at 5, 15, 25, ...; same rises with it.
  always #5 fast = ~fast;
  always #5 same = ~same;
  // slow rises at 20, 60, 100, ...
  initial forever begin
    #10 slow = 1'b0;
    #10 slow = 1'b1;
    #20;
  end

  initial begin
    out0 = 1'b0;
    out1 = 1'b0;
  end
  always @(posedge fast) out0 <= 1'b1;
  always @(posedge slow) out1 <= 1'b1;

  // Both clocks tick together: `##1` moves to the next tick.
  assert property (@(posedge fast) high ##1 @(posedge same) high) begin
    strict_passes++;
    if (strict_first < 0) strict_first = $time;
  end

  // `##0` stays in the time step in which both clocks tick.
  assert property (@(posedge fast) high ##0 @(posedge same) high) begin
    overlap_passes++;
    if (overlap_first < 0) overlap_first = $time;
  end

  // The right operand fails at the first slow tick after each attempt.
  assert property (@(posedge fast) high ##1 @(posedge slow) low)
  else begin
    failures++;
    if (failure_first < 0) failure_first = $time;
  end

  // A fast check, then the first slow tick strictly after it.
  sequence handover;
    @(posedge fast) ##1 out0 ##1 @(posedge slow) out1;
  endsequence
  assert property (handover) begin
    handover_passes++;
    if (handover_first < 0) handover_first = $time;
  end else begin
  end

  initial begin
    #101;
    // fast attempts at 5..95: `##1` passes at 15..95, `##0` at 5..95.
    // Attempts at 5..55 fail at slow ticks 20 and 60 (6 failures), those
    // at 65..95 at slow tick 100 (4 failures).
    // handover: the attempt at 5 checks out0 at 15 (set at 5) and out1 at
    // slow tick 20 (out1 rises with it, so its sampled value is 0): it
    // fails. The attempt at 15 checks out0 at 25 and out1 at 60: it
    // passes, and so do the attempts at 25..45 (at 60) and 55..85 (at 100).
    if (strict_passes != 9 || strict_first != 15
        || overlap_passes != 10 || overlap_first != 5
        || failures != 10 || failure_first != 20
        || handover_passes != 8 || handover_first != 60) begin
      $display("strict %0d@%0d overlap %0d@%0d fail %0d@%0d handover %0d@%0d",
               strict_passes, strict_first, overlap_passes, overlap_first,
               failures, failure_first, handover_passes, handover_first);
      $fatal(1, "multiclocked sequence outcomes differ");
    end
    $display("PASS");
    $finish;
  end
endmodule
