// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 16.5.1, 16.9.3: procedural sampled value functions of
// signals other processes write. clk rises at 5, 15, ..., 55 (ticks k = 1..6)
// and falls at 10, ..., 60 (m = 1..6). cnt counts the rising edges (it is k
// after tick k); a takes cnt[0] at each falling edge, so its samples at the
// ticks are 0, 1, 0, 1, 0, 1. At tick k:
//   $sampled(cnt) = k - 1; $past(cnt) = k - 2 (0 before tick 2);
//   $past(cnt, 2) = k - 3 (0 before tick 3);
//   $rose(a) at k = 2, 4, 6; $fell(a) at k = 3, 5; $stable(a) at k = 1 only
//   (the default before the first tick is a's initial 0); $changed(a) at
//   k >= 2.
// At a falling edge m (no tick of the clock), the Preponed value m differs
// from the last tick's sample m - 1, so $stable(cnt) is 0, and $past(cnt) is
// that last sample, m - 1.
module top;
  logic clk = 0;
  logic [3:0] cnt = 0;
  logic a = 0;
  int errors = 0, ticks = 0, between = 0;
  always #5 clk = ~clk;
  always @(posedge clk) cnt <= cnt + 1;
  always @(negedge clk) a <= cnt[0];
  always @(posedge clk) begin
    ticks++;
    if ($sampled(cnt) != ticks - 1) errors++;
    if ($past(cnt, 1, , @(posedge clk)) != (ticks >= 2 ? ticks - 2 : 0)) errors++;
    if ($past(cnt, 2, , @(posedge clk)) != (ticks >= 3 ? ticks - 3 : 0)) errors++;
    if ($rose(a, @(posedge clk)) !== (ticks >= 2 && ticks % 2 == 0)) errors++;
    if ($fell(a, @(posedge clk)) !== (ticks >= 3 && ticks % 2 == 1)) errors++;
    if ($stable(a, @(posedge clk)) !== (ticks == 1)) errors++;
    if ($changed(a, @(posedge clk)) !== (ticks >= 2)) errors++;
  end
  always @(negedge clk) begin
    between++;
    if ($stable(cnt, @(posedge clk)) !== 1'b0) errors++;
    if ($past(cnt, 1, , @(posedge clk)) != between - 1) errors++;
  end
  initial begin
    #62;
    if (errors == 0 && ticks == 6 && between == 6)
      $display("inl PASS");
    else
      $display("inl FAIL errors %0d ticks %0d between %0d", errors, ticks, between);
    $finish;
  end
endmodule
