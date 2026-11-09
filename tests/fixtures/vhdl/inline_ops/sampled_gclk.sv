// SPDX-License-Identifier: Apache-2.0
//
// The global clocking future sampled value functions (IEEE 1800-2023 16.9.4)
// in concurrent assertions, on signals that hold their values (so the value
// at the next global clock tick is the value now): $future_gclk is the
// signal's value, which the compiled kernel reads like any current value (a
// wide signal as chunks);
// $rising_gclk and $falling_gclk are false, $steady_gclk true and
// $changing_gclk false for a value that does not change.
// With w = 100'h5_0000_0000_0000_0000_0000_0003 and n = 8'hA5 held from time
// 0, each of the 20 clock ticks (the posedge at 10 k + 5, k = 0 .. 19) sees:
//   $future_gclk(w) == w0 and $future_gclk(n) == 8'hA5   -> 20 each
//   $steady_gclk(w) and $steady_gclk(n)                  -> 20 each
//   $rising_gclk(w), $falling_gclk(w), $changing_gclk(n) -> 0 each
module top;
  bit clk = 0;
  always #5 clk = ~clk;
  global clocking gck @(posedge clk); endclocking
  logic [99:0] w = 100'h5_0000_0000_0000_0000_0000_0003;
  logic [7:0] n = 8'hA5;
  int fw = 0, fn = 0, sw = 0, sn = 0, rw = 0, dw = 0, cn = 0;
  cover property (@(posedge clk) $future_gclk(w) == 100'h5_0000_0000_0000_0000_0000_0003) fw++;
  cover property (@(posedge clk) $future_gclk(n) == 8'hA5) fn++;
  cover property (@(posedge clk) $steady_gclk(w)) sw++;
  cover property (@(posedge clk) $steady_gclk(n)) sn++;
  cover property (@(posedge clk) $rising_gclk(w)) rw++;
  cover property (@(posedge clk) $falling_gclk(w)) dw++;
  cover property (@(posedge clk) $changing_gclk(n)) cn++;
  initial begin
    #200;
    $display("inl gclk fw=%0d fn=%0d sw=%0d sn=%0d rw=%0d dw=%0d cn=%0d", fw, fn, sw, sn, rw, dw, cn);
    $finish;
  end
endmodule
