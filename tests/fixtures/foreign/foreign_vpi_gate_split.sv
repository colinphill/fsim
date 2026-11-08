// SPDX-License-Identifier: Apache-2.0
// A put on a gate declared without a delay splits it into a delay slot (IEEE
// 1800-2023 38.32, 28.16). The plug-in puts rise 3, fall 5 on g at 10 ns; the
// turn-off delay of two delays is the smaller, 3. h is never put and stays
// delay free.
// More outputs the split must take (owner decision 2026-11-07): gv drives one
// bit of the vector vec (the delayed write lands in the vector's leaf), gm is
// one of two drivers of m (a net the host resolves; gn drives it z), and gd
// drives dead, which nothing in the design reads: dead logic that the first
// put makes live (the plug-in reads dead through VPI at 21, 26 and 47). The
// plug-in puts rise 3, fall 5 on each of g, gv, gm and gd at 10 ns.
`timescale 1ns / 1ns
module top;
  reg a, b, en, drive;
  wire y, z, bus, t1, m, dead;
  wire [3:0] vec;
  buf g (y, a);
  and h (z, a, b);
  assign bus = drive;
  buf gv (vec[2], a);
  buf gw (vec[1], a);
  buf gm (m, a);
  bufif1 gn (m, 1'b1, 1'b0);
  buf gd (dead, a);
  // A tranif declared without a delay: the put gives its control a delayed
  // copy. The plug-in puts turn-on 3 and turn-off 5, so with en rising at 20
  // and falling at 40 the switch conducts from 23 to 45.
  tranif1 x1 (bus, t1, en);
  initial begin
    a = 0; b = 1; en = 0; drive = 1;
    #20 a = 1; en = 1;
    // g: a rises at 20, so y rises at 20 + 3 = 23; h is immediate. gw is
    // delay free, so vec[1] rises at 20 and vec[2] (gv) at 23: vec[2:1] is
    // 01 at 22 and 11 at 23. m follows gm: 0 at 22, 1 at 23.
    #2 $strobe("#c 22 y=%b z=%b t1=%b vec=%b m=%b", y, z, t1, vec[2:1], m);
    #1 $strobe("#c 23 y=%b z=%b t1=%b vec=%b m=%b", y, z, t1, vec[2:1], m);
    // a falls at 40, so y falls at 40 + 5 = 45; vec[1] falls at 40, vec[2]
    // at 45: vec[2:1] is 10 at 44 and 00 at 45; m is 1 at 44 and 0 at 45.
    #17 a = 0; en = 0;
    #4 $strobe("#c 44 y=%b z=%b t1=%b vec=%b m=%b", y, z, t1, vec[2:1], m);
    #1 $strobe("#c 45 y=%b z=%b t1=%b vec=%b m=%b", y, z, t1, vec[2:1], m);
    #5 $finish;
  end
endmodule
