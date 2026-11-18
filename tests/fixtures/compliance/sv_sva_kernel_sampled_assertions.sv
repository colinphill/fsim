// fsim: top=sv_sva_kernel_sampled_assertions
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 16.5.1, 16.9.3, 16.14.1: concurrent assertions whose
// sampled value functions read signals another process writes. clk rises at
// 5, 15, ..., 195 (ticks k = 1..20); at each falling edge cyc counts and a,
// b, c, d take bits of its old value j, so the samples at tick k >= 2 are
// those of j = k - 2: a = j[0], b = j[1], c = j[2] ^ j[0], d = j[3] (all 0
// at tick 1). The attempts of a_past_rose start where a is 1 (odd k >= 3)
// and need b two ticks back and a rise of c at k + 1:
//   k = 3, 7, 11, 15, 19: b at k - 1 is 0;
//   k = 5, 13: c is 1 at both k and k + 1 (no rise);
//   k = 9, 17: c is 0 at k + 1;
// so all 9 attempts fail. a_change holds at every tick: d changes only at
// k = 10 and 18, where a falls.
module sv_sva_kernel_sampled_assertions;
  logic clk = 0;
  always #5 clk = ~clk;
  logic a = 0, b = 0, c = 0, d = 0;
  int cyc = 0;
  int n_past_rose = 0, n_change = 0;
  always @(negedge clk) begin
    cyc <= cyc + 1;
    a <= cyc[0];
    b <= cyc[1];
    c <= cyc[2] ^ cyc[0];
    d <= cyc[3];
  end
  a_past_rose: assert property (@(posedge clk) a |=> $past(b, 2) && $rose(c))
  else
    n_past_rose++;
  a_change: assert property (@(posedge clk) $stable(d) || $fell(a) || $changed(b))
  else
    n_change++;
  initial begin
    #198;
    if (n_past_rose == 9 && n_change == 0)
      $display("PASS");
    else
      $display("FAIL past_rose %0d change %0d", n_past_rose, n_change);
    $finish;
  end
endmodule
