// SPDX-License-Identifier: Apache-2.0
// Assertion callbacks on sequence and property instances (IEEE 1800-2023
// 39.4.2: cbAssertionStart, cbAssertionSuccess and cbAssertionFailure on a
// vpiSequenceInst or vpiPropertyInst).
//
// clk rises at 5, 15, 25 and 35 and falls at 10, 20 and 30. The values
// sampled at the edges:
//   posedge 5:  a 1, b 0        negedge 10: a 1, c 1
//   posedge 15: a 1, b 1        negedge 20: a 1, c 0
//   posedge 25: a 0, b 1        negedge 30: a 0, c 0
//   posedge 35: a 0, b 0
// a_seq evaluates the instance s_ab (a ##1 b) once per attempt, from the
// attempt's start: the attempt of 5 matches at 15, the one of 15 at 25, and
// the ones of 25 and 35 fail at once (a sampled 0). a_prop is the instance
// p_ac (a |-> c at negedge clk): it passes at 10, fails at 20 and passes
// vacuously at 30. Each evaluation reports its start at its first tick, and
// its match or failure at the tick it is decided; attemptStartTime is the
// start of the assertion attempt (39.4.2 d), cb_time the time of the event.
// The library removes the callbacks on s_ab at 28, so the attempt of 35
// reports nothing; cbAssertionVacuousSuccess is not a reason of an instance
// and is refused.
module top;
  logic clk = 0, a = 0, b = 0, c = 0;
  always #5 clk = ~clk;
  initial begin
    a = 1; c = 1;
    #10 begin b = 1; c = 0; end
    #10 a = 0;
    #10 b = 0;
    #8 $finish;
  end

  sequence s_ab;
    a ##1 b;
  endsequence

  property p_ac;
    @(negedge clk) a |-> c;
  endproperty

  a_seq: assert property (@(posedge clk) s_ab);
  a_prop: assert property (p_ac);
endmodule
