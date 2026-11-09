// fsim: top=sv_property_kinds_checked std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 16.12: the property operators of the concurrent assertion
// monitor (not, and, or, implies, iff, if/else, nexttime, always, s_eventually,
// until, until_with, accept_on) and the sequence operators throughout,
// first_match, intersect and within, each asserted (the failures are counted
// by the else action) and covered (every successful attempt, vacuous ones
// included, 16.14.3) over 16 clock ticks. Sample n (the posedge at 10 n + 5)
// sees the table value n - 1 applied at the negedge before it, and 0 for
// n = 0 (16.5.1). Attempts not decided when the run ends report nothing
// (16.12.2). The expected counts are derived by evaluating each operator
// over the sampled table directly:
//
//   q1  always a                             fail 14, success  0
//   q2  nexttime a                           fail  6, success  9
//   q3  a until b                            fail  4, success 11
//   q4  a until_with b                       fail  9, success  6
//   q5  s_eventually [1:3] b                 fail  0, success 14
//   q6  always [1:2] c                       fail 10, success  4
//   q7  if (a) b else c                      fail  9, success  7
//   q8  (a ##1 b) and (b ##1 c)              fail 13, success  3
//   q9  (a ##1 b) or (b ##1 c)               fail  5, success 10
//   q10 a implies b                          fail  5, success 11
//   q11 a iff b                              fail  9, success  7
//   q12 accept_on (d) (a |-> ##2 b)          fail  2, success 12
//   q13 c throughout (a ##1 b)               fail 14, success  1
//   q14 first_match(a ##[1:2] b)             fail  7, success  7
//   q15 (a ##[1:2] b) intersect (c[*2:3])    fail 13, success  1
//   q16 b within (a ##2 c)                   fail  8, success  6
module sv_property_kinds_checked;
  bit clk = 0;
  always #5 clk = ~clk;
  logic a = 0, b = 0, c = 0, d = 0;
  int cyc = 0;
  logic ta [0:15] = '{1'b1, 1'b1, 1'b0, 1'b1, 1'b0, 1'b0, 1'b1, 1'b1, 1'b1, 1'b0, 1'b0, 1'b1, 1'b0, 1'b1, 1'b1, 1'b0};
  logic tb [0:15] = '{1'b0, 1'b1, 1'b1, 1'b0, 1'b0, 1'b1, 1'b0, 1'b1, 1'b1, 1'b1, 1'b0, 1'b0, 1'b1, 1'b1, 1'b0, 1'b0};
  logic tc [0:15] = '{1'b0, 1'b0, 1'b1, 1'b1, 1'b0, 1'b0, 1'b0, 1'b0, 1'b1, 1'b1, 1'b1, 1'b0, 1'b0, 1'b1, 1'b1, 1'b0};
  logic td [0:15] = '{1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b1, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0, 1'b0};
  always @(negedge clk) begin
    a = ta[cyc]; b = tb[cyc]; c = tc[cyc]; d = td[cyc];
    cyc <= cyc + 1;
  end
  int f1 = 0, s1 = 0, f2 = 0, s2 = 0, f3 = 0, s3 = 0, f4 = 0, s4 = 0, f5 = 0, s5 = 0, f6 = 0, s6 = 0, f7 = 0, s7 = 0, f8 = 0, s8 = 0, f9 = 0, s9 = 0, f10 = 0, s10 = 0, f11 = 0, s11 = 0, f12 = 0, s12 = 0, f13 = 0, s13 = 0, f14 = 0, s14 = 0, f15 = 0, s15 = 0, f16 = 0, s16 = 0;
  assert property (@(posedge clk) always a) else f1++;
  cover property (@(posedge clk) always a) s1++;
  assert property (@(posedge clk) nexttime a) else f2++;
  cover property (@(posedge clk) nexttime a) s2++;
  assert property (@(posedge clk) a until b) else f3++;
  cover property (@(posedge clk) a until b) s3++;
  assert property (@(posedge clk) a until_with b) else f4++;
  cover property (@(posedge clk) a until_with b) s4++;
  assert property (@(posedge clk) s_eventually [1:3] b) else f5++;
  cover property (@(posedge clk) s_eventually [1:3] b) s5++;
  assert property (@(posedge clk) always [1:2] c) else f6++;
  cover property (@(posedge clk) always [1:2] c) s6++;
  assert property (@(posedge clk) if (a) b else c) else f7++;
  cover property (@(posedge clk) if (a) b else c) s7++;
  assert property (@(posedge clk) (a ##1 b) and (b ##1 c)) else f8++;
  cover property (@(posedge clk) (a ##1 b) and (b ##1 c)) s8++;
  assert property (@(posedge clk) (a ##1 b) or (b ##1 c)) else f9++;
  cover property (@(posedge clk) (a ##1 b) or (b ##1 c)) s9++;
  assert property (@(posedge clk) a implies b) else f10++;
  cover property (@(posedge clk) a implies b) s10++;
  assert property (@(posedge clk) a iff b) else f11++;
  cover property (@(posedge clk) a iff b) s11++;
  assert property (@(posedge clk) accept_on (d) (a |-> ##2 b)) else f12++;
  cover property (@(posedge clk) accept_on (d) (a |-> ##2 b)) s12++;
  assert property (@(posedge clk) c throughout (a ##1 b)) else f13++;
  cover property (@(posedge clk) c throughout (a ##1 b)) s13++;
  assert property (@(posedge clk) first_match(a ##[1:2] b)) else f14++;
  cover property (@(posedge clk) first_match(a ##[1:2] b)) s14++;
  assert property (@(posedge clk) (a ##[1:2] b) intersect (c[*2:3])) else f15++;
  cover property (@(posedge clk) (a ##[1:2] b) intersect (c[*2:3])) s15++;
  assert property (@(posedge clk) b within (a ##2 c)) else f16++;
  cover property (@(posedge clk) b within (a ##2 c)) s16++;
  int failures = 0;
  task check(input int got, input int expected, input string what);
    if (got !== expected) begin
      $display("FAIL %s: got %0d expected %0d", what, got, expected);
      failures++;
    end
  endtask
  initial begin
    #160;
    check(f1, 14, "always a failures");
    check(s1, 0, "always a successes");
    check(f2, 6, "nexttime a failures");
    check(s2, 9, "nexttime a successes");
    check(f3, 4, "a until b failures");
    check(s3, 11, "a until b successes");
    check(f4, 9, "a until_with b failures");
    check(s4, 6, "a until_with b successes");
    check(f5, 0, "s_eventually [1:3] b failures");
    check(s5, 14, "s_eventually [1:3] b successes");
    check(f6, 10, "always [1:2] c failures");
    check(s6, 4, "always [1:2] c successes");
    check(f7, 9, "if (a) b else c failures");
    check(s7, 7, "if (a) b else c successes");
    check(f8, 13, "(a ##1 b) and (b ##1 c) failures");
    check(s8, 3, "(a ##1 b) and (b ##1 c) successes");
    check(f9, 5, "(a ##1 b) or (b ##1 c) failures");
    check(s9, 10, "(a ##1 b) or (b ##1 c) successes");
    check(f10, 5, "a implies b failures");
    check(s10, 11, "a implies b successes");
    check(f11, 9, "a iff b failures");
    check(s11, 7, "a iff b successes");
    check(f12, 2, "accept_on (d) (a |-> ##2 b) failures");
    check(s12, 12, "accept_on (d) (a |-> ##2 b) successes");
    check(f13, 14, "c throughout (a ##1 b) failures");
    check(s13, 1, "c throughout (a ##1 b) successes");
    check(f14, 7, "first_match(a ##[1:2] b) failures");
    check(s14, 7, "first_match(a ##[1:2] b) successes");
    check(f15, 13, "(a ##[1:2] b) intersect (c[*2:3]) failures");
    check(s15, 1, "(a ##[1:2] b) intersect (c[*2:3]) successes");
    check(f16, 8, "b within (a ##2 c) failures");
    check(s16, 6, "b within (a ##2 c) successes");
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
