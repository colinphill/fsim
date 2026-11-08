// SPDX-License-Identifier: Apache-2.0
//
// Concurrent-assertion evaluators run as compiled kernel members (IEEE
// 1800-2023 16.14): the monitor's tick and outcome pop are the kernel
// instructions sva_tick and sva_outcome, the sampled-value reads ($sampled
// of each boolean, $rose, $fell, $past) are inline loads (sva_sampled_load),
// and the evaluator's own body is native code (the runner forbids the
// `tier:not_compiled:assertion` row and the SvaOperation and ReadSignal
// helpers). Asserts pass and fail, covers count matches, and the sampled
// functions $rose, $fell and $past are read.
//
// Stimulus: rows[k] is applied at the negedge 10 (k + 1); the posedge of
// sample n (t = 10 n + 5) samples the preponed values, which are rows[n - 1]
// for n >= 1 and the initial 000 for n = 0 (16.5.1). Samples 0 .. 15 are
// taken before the final display at t = 160.
//
//   rows (a b c): 0:100 1:010 2:001 3:100 4:000 5:000 6:110 7:011 8:001
//                 9:100 10:010 11:010 12:010 13:001 14:000
//   a per sample n = 0..15: 0 1 0 0 1 0 0 1 0 0 1 0 0 0 0 0
//   b per sample n = 0..15: 0 0 1 0 0 0 0 1 1 0 0 1 1 1 0 0
//
// f1 .. f6 and c1, c2 are the counts of the assertions of the compliance
// case sv_sequence_properties.sv, which has the same stimulus and derives
// them (1, 2, 1, 1, 0, 1 failures; 1 and 6 cover matches).
// $rose(a): a is 1 at n = 1, 4, 7, 10 and 0 in the sample before each
//   -> 4 matches (n = 0 has no earlier sample: the default is 0, not a rise).
// $fell(a): a falls at n = 2, 5, 8, 11 -> 4 matches.
// $past(a) && b: $past(a) is 1 at n = 2, 5, 8, 11; b is 1 at n = 2 and 8 and
//   11 (rows 1, 7, 10) and 0 at n = 5 (row 4) -> 3 matches.
module top;
  bit clk = 0;
  always #5 clk = ~clk;
  logic a = 0, b = 0, c = 0;
  int cyc = 0;
  int f1 = 0, f2 = 0, f3 = 0, f4 = 0, f5 = 0, f6 = 0, c1 = 0, c2 = 0;
  int rose = 0, fell = 0, past = 0;
  logic [2:0] rows [0:15] = '{3'b100, 3'b010, 3'b001, 3'b100, 3'b000,
    3'b000, 3'b110, 3'b011, 3'b001, 3'b100, 3'b010, 3'b010, 3'b010,
    3'b001, 3'b000, 3'b000};
  always @(negedge clk) begin
    {a, b, c} = rows[cyc];
    cyc <= cyc + 1;
  end

  sequence s_bc;
    b ##1 c;
  endsequence

  property p_next(x, y);
    x |=> y;
  endproperty

  property p_clocked(x, y);
    @(posedge clk) x |-> ##1 y;
  endproperty

  assert property (@(posedge clk) a |-> ##[1:2] b) else f1++;
  assert property (@(posedge clk) a |=> s_bc) else f2++;
  assert property (@(posedge clk) a |-> b[->1] ##1 c) else f3++;
  assert property (@(posedge clk) not (b ##1 b ##1 b)) else f4++;
  assert property (@(posedge clk) p_next(b, c || b)) else f5++;
  assert property (p_clocked(a, b)) else f6++;
  cover property (@(posedge clk) a ##1 b[*2]) c1++;
  cover property (@(posedge clk) (a ##1 b) or (b ##1 c)) c2++;
  cover property (@(posedge clk) $rose(a)) rose++;
  cover property (@(posedge clk) $fell(a)) fell++;
  cover property (@(posedge clk) $past(a) && b) past++;

  initial begin
    #160;
    $display("inl asrt fail=%0d,%0d,%0d,%0d,%0d,%0d cover=%0d,%0d rose=%0d fell=%0d past=%0d",
      f1, f2, f3, f4, f5, f6, c1, c2, rose, fell, past);
    $finish;
  end
endmodule
