// SPDX-License-Identifier: Apache-2.0
// The failing expression of a rejected attempt (IEEE 1800-2023 39.4.2:
// cbAssertionFailure carries detail.failExpr, the expression of the property
// where the attempt failed; 37.50). clk rises at 5, 15, 25 and 35, and the
// stimulus changes 3 ns before each edge, so the sampled values are
//   edge  5: a=0 b=0 c=1
//   edge 15: a=1 b=0 c=1
//   edge 25: a=0 b=1 c=1
//   edge 35: a=0 b=0 c=1
// p_imp (a |-> b): the antecedent holds at 15 only, and the consequent b is
// false there: the attempt of 15 fails at b. The other attempts are vacuous.
// p_seq (a ##1 b): the attempt of 15 matches (b at 25). The attempts of 5, 25
// and 35 fail at the first check, a.
// p_not (not c) runs on clk2, which rises at 7, 17, 27 and 37: c holds at
// every edge, so the sequence matches and the negation fails; the attempts of
// 7, 17, 27 and 37 fail, reported at c, the leftmost expression of the
// property. The failing expression of a plain name is the object it denotes
// (37.50), so the plug-in prints its full name.
module top;
  logic clk = 0, clk2 = 0, a = 0, b = 0, c = 1;
  always #5 clk = ~clk;
  initial begin
    #2;
    forever #5 clk2 = ~clk2;
  end

  p_imp: assert property (@(posedge clk) a |-> b);
  p_seq: assert property (@(posedge clk) a ##1 b);
  p_not: assert property (@(posedge clk2) not c);

  initial begin
    #12 a = 1;
    #10 a = 0; b = 1;
    #10 b = 0;
    #8 $finish;  // 40; the attempt of clk2 at 37 is decided by then
  end
endmodule
