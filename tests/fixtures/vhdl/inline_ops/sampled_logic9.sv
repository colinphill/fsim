// SPDX-License-Identifier: Apache-2.0
//
// Sampled values of nets that a VHDL top level drives with std_logic
// (sampled_logic9_vhdl.vhd), read in concurrent assertions by the compiled
// kernel from its nine-value history rings (IEEE 1800-2023 16.5.1, 16.9.3;
// 21.2.1.3 for the VHDL-to-SystemVerilog value mapping: 'U', 'W' and '-' read
// as x, 'L' as 0, 'H' as 1, in every sampled read). sb is one std_logic, sv an 80 element vector.
// Sample n (the posedge at 10 n + 5) sees the value the VHDL process applied
// at 10 n, and the unassigned 'U' for n = 0; the default of a history that is
// not that long is that initial value.
//
//   $rose(sb), $fell(sb)   the mapped value is 1 (0) and was not in the sample
//                          before: 8 and 10
//   $sampled(sb) === 1'b1  the mapped value is 1 ('1' or 'H'): 10
//   sb === 1'b1 in procedural code reads the mapped value too: plain = one
//   $stable(sv), $changed(sv)  every element as in the sample before, nine
//                          value codes compared (the table never moves between
//                          two codes that map to the same value): 5 and 35
//   $past(sv) === {80{1}}  the sample before maps to all 1 (patterns "1" and
//                          "H"): 12
//   $sampled(sv) === {16{x}, 64{1}}, {16{z}, 64{0}}  the patterns "X"x16 & "1"x64
//                          and "Z"x16 & "0"x64: 7 and 8
`timescale 1ns/1ps
module svm(input wire sb, input wire [79:0] sv);
  bit clk = 0;
  always #5 clk = ~clk;
  int rose = 0, fell = 0, stable = 0, changed = 0, one = 0, past1 = 0, x1 = 0, z0 = 0, plain = 0;
  always @(posedge clk) if (sb === 1'b1) plain++;
  cover property (@(posedge clk) $rose(sb)) rose++;
  cover property (@(posedge clk) $fell(sb)) fell++;
  cover property (@(posedge clk) $sampled(sb) === 1'b1) one++;
  cover property (@(posedge clk) $stable(sv)) stable++;
  cover property (@(posedge clk) $changed(sv)) changed++;
  cover property (@(posedge clk) $past(sv) === {80{1'b1}}) past1++;
  cover property (@(posedge clk) $sampled(sv) === {{16{1'bx}}, {64{1'b1}}}) x1++;
  cover property (@(posedge clk) $sampled(sv) === {{16{1'bz}}, {64{1'b0}}}) z0++;
  initial begin
    #400;
    $display("inl l9 rose=%0d fell=%0d stable=%0d changed=%0d one=%0d past1=%0d x1=%0d z0=%0d plain=%0d",
      rose, fell, stable, changed, one, past1, x1, z0, plain);
    $finish;
  end
endmodule
