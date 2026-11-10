// SPDX-License-Identifier: Apache-2.0
//
// The global clocking comparing functions (IEEE 1800-2023 16.9.4: $steady_gclk
// is $sampled(e) == $future_gclk(e), $changing_gclk its negation,
// $rising_gclk and $falling_gclk the LSB's change to 1 and to 0) on nets a
// VHDL top level drives with std_logic (sampled_gclk_logic9_vhdl.vhd). A
// SystemVerilog reader compares the values it reads: the mixed contract's
// 21.2.1.3 / UG900 mapping ('U' and 'X' read as x, 'L' as 0, 'H' as 1, 'Z'
// as z; strength is not part of the value), so 'L' after '0', '1' after 'H'
// and 'Z' after 'Z' are steady.
//
// The VHDL side sets clk to '1' at 10 k ns (k = 1 .. 8, '0' again at
// 10 k + 5) and, in the same delta, sb to p(k) and element 65 of v to p(k);
// the other elements of v are '0' from 10 ns. At the tick at 10 k the
// sampled value is p(k - 1) ('U' for k = 1, all of v 'U' then) and the
// value at the next global clock tick is p(k):
//
//   k (t)    1 (10) 2 (20) 3 (30) 4 (40) 5 (50) 6 (60) 7 (70) 8 (80)
//   p(k-1)   'U'    '0'    'L'    'H'    '1'    'Z'    'Z'    '1'
//   p(k)     '0'    'L'    'H'    '1'    'Z'    'Z'    '1'    'L'
//   read     x->0   0->0   0->1   1->1   1->z   z->z   z->1   1->0
//   steady   0      1      0      1      0      1      0      0
//   rising   0      0      1      0      0      0      1      0
//   falling  1      0      0      0      0      0      0      1
//
// Each cover adds the time of its successes: $steady_gclk 20 + 40 + 60 =
// 120, $changing_gclk 10 + 30 + 50 + 70 + 80 = 240, $rising_gclk 30 + 70 =
// 100, $falling_gclk 10 + 80 = 90, for sb and for v alike (v's other
// elements read 0 from the first tick's future on, x before).
`timescale 1ns/1ns
module svg(input wire clk, input wire sb, input wire [69:0] v);
  global clocking gck @(posedge clk); endclocking
  int steady = 0, changing = 0, rising = 0, falling = 0;
  int vsteady = 0, vchanging = 0;
  cover property (@(posedge clk) $steady_gclk(sb)) steady += $time;
  cover property (@(posedge clk) $changing_gclk(sb)) changing += $time;
  cover property (@(posedge clk) $rising_gclk(sb)) rising += $time;
  cover property (@(posedge clk) $falling_gclk(sb)) falling += $time;
  cover property (@(posedge clk) $steady_gclk(v)) vsteady += $time;
  cover property (@(posedge clk) $changing_gclk(v)) vchanging += $time;
  initial begin
    #100;
    $display("inl gclk9 steady=%0d changing=%0d rising=%0d falling=%0d vsteady=%0d vchanging=%0d",
      steady, changing, rising, falling, vsteady, vchanging);
    $finish;
  end
endmodule
