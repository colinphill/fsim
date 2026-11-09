// SPDX-License-Identifier: Apache-2.0
//
// Sampled values of a 100 bit signal in concurrent assertions, which the
// compiled kernel reads as chunked wide values (IEEE 1800-2023 16.5.1,
// 16.9.3): $sampled, $past with a depth, $rose and $fell (the least
// significant bit of the value, 16.9.3), and $stable and $changed (every bit;
// the table changes bits above 64 alone, so a one word compare would miss
// them). Sample n (the posedge at 10 n + 5) sees rows[n - 1], and the
// initial value 0 for n = 0 (16.5.1); the default of a history that is not
// that long is the initial value.
//
//   $rose(w):   bit 0 is 1 and was not 1 in the sample before   -> 9
//   $fell(w):   bit 0 is 0 and was not 0 in the sample before   -> 8
//   $stable(w): every bit as in the sample before               -> 7
//   $changed(w): some bit differs from the sample before        -> 33
//   $past(w, 2) == K and $sampled(w) == K, K = 100'h8000000000000000000000001 -> 2 and 3
module top;
  bit clk = 0;
  always #5 clk = ~clk;
  logic [99:0] w = 0;
  int cyc = 0;
  int rose = 0, fell = 0, stable = 0, changed = 0, past2 = 0, samp = 0;
  logic [99:0] rows [0:39] = '{100'h0000000000000000000000001, 100'h3000000000000000000000006, 100'h0000000005000000000000000, 100'h0000000005000000000000000, 100'h0000000018000000000000000, 100'h8000000000000000000000000, 100'h3000000000000000000000006, 100'h8000000000000000000000001, 100'h0000000005000000000000000, 100'h3000000000000000000000006, 100'h0000000018000000000000000, 100'h3000000000000000000000006, 100'h0000000000000000000000001, 100'h8000000000000000000000000, 100'h0000000000000000000000000, 100'h8000000000000000000000000, 100'h0000000018000000000000000, 100'h0000000018000000000000000, 100'h0000000000000000000000001, 100'h0000000000000000000000000, 100'h0000000000000000000000001, 100'h0000000000000000000000001, 100'h3000000000000000000000006, 100'h3000000000000000000000006, 100'h0000000005000000000000000, 100'h0000000000000000000000001, 100'h0000000000000000000000000, 100'h0000000000000000000000001, 100'h0000000018000000000000000, 100'h3000000000000000000000006, 100'h0000000005000000000000000, 100'h0000000005000000000000000, 100'h3000000000000000000000006, 100'h8000000000000000000000001, 100'h8000000000000000000000000, 100'h0000000005000000000000000, 100'h0000000005000000000000000, 100'h8000000000000000000000000, 100'h8000000000000000000000001, 100'h8000000000000000000000000};
  always @(negedge clk) begin
    w = rows[cyc];
    cyc <= cyc + 1;
  end
  cover property (@(posedge clk) $rose(w)) rose++;
  cover property (@(posedge clk) $fell(w)) fell++;
  cover property (@(posedge clk) $stable(w)) stable++;
  cover property (@(posedge clk) $changed(w)) changed++;
  cover property (@(posedge clk) $past(w, 2) == 100'h8000000000000000000000001) past2++;
  cover property (@(posedge clk) $sampled(w) == 100'h8000000000000000000000001) samp++;
  initial begin
    #400;
    $display("inl wide rose=%0d fell=%0d stable=%0d changed=%0d past2=%0d samp=%0d",
      rose, fell, stable, changed, past2, samp);
    $finish;
  end
endmodule
