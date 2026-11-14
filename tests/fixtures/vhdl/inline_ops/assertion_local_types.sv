// SPDX-License-Identifier: Apache-2.0
// Local variables of string and real types, a multiclocked property's
// initialization assignment and an expect statement with locals and a
// subroutine call (IEEE 1800-2023 16.10, 16.11, 16.13.7, 16.17) on the
// concurrent-property monitor. clk rises at 5, 15, 25, 35; c2 at 7, 21, 35;
// the reports print at 42. The expected values, from the LRM:
//   str: a string local takes s's sampled value at each attempt and is
//     compared one tick later: s is "x", "y" from 11, "x" from 21, so the
//     attempts of 5 and 25 pass (at 15, 35) and the one of 15 fails at 25.
//   mix: a string and a real local, both updated by a match item one tick
//     after the antecedent (ls = {ls, "!"}, lr = lr * 2) and checked one
//     tick later: the attempt of 5 sees "ab", 1.5 and passes at 25; the one
//     of 15 sees "cd" (from 11) and fails at 35.
//   mc: logic [3:0] v = d in a property whose only semantic leading clock is
//     clk, with ##1 @(posedge c2): v takes d's sampled value at the clk tick
//     where the attempt begins (16.13.7), not at the c2 tick. d is 2, then
//     9 from 6: the attempt of 5 passes at 7, those of 15 and 25 fail at 21
//     and 35.
//   ex: an expect statement started at 7 (16.17) waits for the next clk
//     tick (15): ae is 1 there, v = de (3), ls = se ("q"), and note(se, de)
//     runs at that end point (16.11) with sampled arguments; at 25 be is 1,
//     de is 4 == v + 1 and ls is still "q" though se is "r" from 16: the
//     expect passes at 25 and its procedure resumes there.
module top;
  logic clk = 0, c2 = 0;
  always #5 clk = ~clk;
  always #7 c2 = ~c2;

  logic a = 0;
  string s = "x";
  string sm = "ab";
  real r = 1.5;
  logic [3:0] d = 2;
  logic b = 0;
  logic ae = 0, be = 0;
  logic [3:0] de = 0;
  string se = "q";

  int n_str = 0, n_mix = 0, n_mc = 0;
  string f_str = "", f_mix = "", f_mc = "";
  string ex = "", calls = "";

  task automatic note(string x, int y);
    calls = $sformatf("%s%s%0d@%0t,", calls, x, y, $time);
  endtask

  property p_str;
    string ls;
    @(posedge clk) (a, ls = s) ##1 (ls == "x");
  endproperty
  property p_mix;
    string ls; real lr;
    @(posedge clk) (a, ls = sm, lr = r) ##1 (1, ls = {ls, "!"}, lr = lr * 2)
      ##1 (ls == "ab!" && lr == 3.0);
  endproperty
  property p_mc;
    logic [3:0] v = d;
    @(posedge clk) a ##1 @(posedge c2) (b && v == 2);
  endproperty
  property p_ex;
    logic [3:0] v; string ls;
    @(posedge clk) (ae, v = de, ls = se, note(se, de))
      ##1 (be && de == v + 1 && ls == "q");
  endproperty

  str: assert property (p_str) n_str++;
    else f_str = $sformatf("%s%0t,", f_str, $time);
  mix: assert property (p_mix) n_mix++;
    else f_mix = $sformatf("%s%0t,", f_mix, $time);
  mc: assert property (p_mc) n_mc++;
    else f_mc = $sformatf("%s%0t,", f_mc, $time);

  initial begin
    #1 a = 1; b = 1;
    #5 d = 9; ae = 1; de = 3;
    #5 s = "y"; sm = "cd";
    #5 be = 1; de = 4; se = "r";
    #5 s = "x";
  end
  initial begin
    #7 expect (p_ex) ex = $sformatf("pass@%0t", $time);
      else ex = $sformatf("fail@%0t", $time);
    ex = $sformatf("%s after@%0t", ex, $time);
  end
  initial begin
    #42;
    $display("inl str pass=%0d fail=%s", n_str, f_str);
    $display("inl mix pass=%0d fail=%s", n_mix, f_mix);
    $display("inl mc pass=%0d fail=%s", n_mc, f_mc);
    $display("inl ex %s calls=%s", ex, calls);
    $finish;
  end
endmodule
