// SPDX-License-Identifier: Apache-2.0
//
// disable iff is asynchronous and reads current values (IEEE 1800-2023
// 16.12): an attempt is disabled when the disable condition is true at any
// time from its start in the Observed region to its end, both inclusive,
// not only at the ticks of its clock. A default disable iff (16.15) behaves
// the same, and $assertcontrol Kill (20.11) ends the attempts in progress.
//
// p1 checks a |-> ##2 b under disable iff (rst); p2 checks a |-> ##1 b
// under the default disable iff (rst2). b stays 0, so every attempt that
// starts (a sampled 1 at the ticks 15, 55, 95, 105, 155, 185, 195, 205,
// 245, 285, 295) fails at its end unless it is disabled or killed:
//   22..23  rst pulse between ticks: p1's attempt from 15 is disabled
//   62      rst rises and falls within the step (#0): p1's attempt from 55
//   105     the same glitch at the tick's step before the Observed region:
//           p1's attempt from 95 is disabled, the one starting at 105 is not
//   140     rst pulse while no attempt is in progress: no effect
//   195     rst rises at the tick: p1's attempts from 185 and 195 are
//           disabled; it falls at the tick at 205, so the attempt starting
//           there is not (current value, not the sampled one)
//   252     rst2 pulse: p2's attempt from 245 (p1's is not)
//   288     rst pulse, then Kill at 290 and On at 291: the attempts from
//           285 end; the attempts starting at 295 are not affected by the
//           earlier pulse
//
// g.p3 checks a |-> ##3 b under disable iff (rst) in a generate block (its
// disable condition is the block's): the attempts from 15, 55, 95, 185, 195
// and 285 are disabled as p1's are, the others fail 30 after their start.
module top;
  bit clk, a, b, rst, rst2;
  default disable iff (rst2);
  always #5 clk = ~clk;

  function automatic bit starts(input int tick);
    case (tick)
      15, 55, 95, 105, 155, 185, 195, 205, 245, 285, 295: return 1'b1;
      default: return 1'b0;
    endcase
  endfunction
  always @(negedge clk) a <= starts(int'($time) + 5);

  p1: assert property (@(posedge clk) disable iff (rst) a |-> ##2 b)
    else $display("inl p1 fail t=%0t", $time);
  p2: assert property (@(posedge clk) a |-> ##1 b)
    else $display("inl p2 fail t=%0t", $time);
  if (1) begin : g
    p3: assert property (@(posedge clk) disable iff (rst) a |-> ##3 b)
      else $display("inl p3 fail t=%0t", $time);
  end

  initial begin
    #22 rst = 1;
    #1 rst = 0;
    #39 rst = 1;   // 62
    #0 rst = 0;
    #43 rst = 1;   // 105
    #0 rst = 0;
    #35 rst = 1;   // 140
    #1 rst = 0;
    #54 rst = 1;   // 195
    #10 rst = 0;   // 205
    #47 rst2 = 1;  // 252
    #1 rst2 = 0;
    #35 rst = 1;   // 288
    #1 rst = 0;
    #1 $assertcontrol(5);  // 290: Kill
    #1 $assertcontrol(3);  // 291: On
    #49 $finish;
  end
endmodule
