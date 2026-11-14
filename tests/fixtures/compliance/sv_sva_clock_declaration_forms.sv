// fsim: top=sv_sva_clock_declaration_forms
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 16.8, 16.12, 16.13.1, 16.14.7, 16.16, 16.16.1, A.6.5:
// legal forms of sequence and property declarations and their clocks.
//   - an event actual substituted into `@(e)` is a parenthesized event
//     expression, `@((posedge clk))` (A.6.5);
//   - an event formal whose default is $inferred_clock takes the clock of
//     the instance's context: a leading clock or a default clocking;
//   - a let instance and a cast in an assertion expression;
//   - a sequence instance as the actual of another (`s2(s1(a))` where s2's
//     body instances s1) is no recursion;
//   - without a clock from the statement, the maximal property instance's
//     unique semantic leading clock clocks it (16.16 f), also through an
//     antecedent `(@(c) a) |=> b`;
//   - sequences declared in $unit, a package (imported and qualified, a
//     package sequence naming another of its package) and a generate block.
// clk rises at 5, 15, ..., 95; clk2 at 7, 21, 35, 49, 63, 77, 91. Every
// operand is 1, so:
//   one-tick covers: 10 attempts (5..95) match;
//   two-tick sequences: attempts 5..85 match (9);
//   p_nested(a) = a ##1 b ##1 b ##1 c: attempts 5..65 (7);
//   p_multi = (@clk a ##1 b) ##1 (@clk2 b ##1 c): the clk2 part starts at
//     the first clk2 tick after the clk part ends, so attempts 5..65 end
//     at 35, 49, 63, 63, 77, 91, 91 (7);
//   p_antecedent = (@clk a) |=> b: attempts 5..85 (9).
package sva_forms_pkg;
  sequence pk_two(x);
    x ##1 x;
  endsequence
  sequence pk_uses;
    pk_two(1'b1);
  endsequence
endpackage

sequence unit_two(x);
  x ##1 x;
endsequence

module sva_forms_default(input logic clk, output int n);
  logic a = 1;
  default clocking cb @(posedge clk); endclocking
  property p_inferred(event e = $inferred_clock);
    @(e) a;
  endproperty
  initial n = 0;
  cover property (p_inferred()) n++;
endmodule

module sv_sva_clock_declaration_forms;
  import sva_forms_pkg::pk_two;
  logic clk = 0, clk2 = 0, a = 1, b = 1, c = 1;
  int d = 3;
  always #5 clk = ~clk;
  always #7 clk2 = ~clk2;
  int n_event, n_inferred, n_let, n_cast, n_nested, n_multi, n_antecedent;
  int n_unit, n_pkg, n_pkg_qualified, n_gen, n_default;
  let incr(v) = v + 1;
  property p_event(e);
    @(e) a;
  endproperty
  property p_inferred(event e = $inferred_clock);
    @(e) a;
  endproperty
  sequence s1(x);
    x ##1 b;
  endsequence
  sequence s2(x);
    s1(x) ##1 c;
  endsequence
  property p_nested(x);
    @(posedge clk) s2(s1(x));
  endproperty
  sequence m1;
    @(posedge clk) a ##1 b;
  endsequence
  sequence m2;
    @(posedge clk2) b ##1 c;
  endsequence
  property p_multi;
    m1 ##1 m2;
  endproperty
  property p_antecedent;
    (@(posedge clk) a) |=> b;
  endproperty
  cover property (p_event(posedge clk)) n_event++;
  cover property (@(posedge clk) p_inferred()) n_inferred++;
  cover property (@(posedge clk) incr(d) == 4) n_let++;
  cover property (@(posedge clk) int'(b) == 1) n_cast++;
  cover property (p_nested(a)) n_nested++;
  cover property (p_multi) n_multi++;
  cover property (p_antecedent) n_antecedent++;
  cover property (@(posedge clk) unit_two(a)) n_unit++;
  cover property (@(posedge clk) pk_two(a)) n_pkg++;
  cover property (@(posedge clk) sva_forms_pkg::pk_uses) n_pkg_qualified++;
  generate
    if (1) begin : g
      sequence gs;
        a ##1 b;
      endsequence
      cover property (@(posedge clk) gs) n_gen++;
    end
  endgenerate
  sva_forms_default u_default(.clk(clk), .n(n_default));
  initial begin
    #100;
    if (n_event == 10 && n_inferred == 10 && n_let == 10 && n_cast == 10
        && n_nested == 7 && n_multi == 7 && n_antecedent == 9
        && n_unit == 9 && n_pkg == 9 && n_pkg_qualified == 9 && n_gen == 9
        && n_default == 10)
      $display("PASS");
    else
      $display("FAIL event %0d inferred %0d let %0d cast %0d nested %0d multi %0d antecedent %0d unit %0d pkg %0d pkgq %0d gen %0d default %0d",
        n_event, n_inferred, n_let, n_cast, n_nested, n_multi, n_antecedent,
        n_unit, n_pkg, n_pkg_qualified, n_gen, n_default);
    $finish;
  end
endmodule
