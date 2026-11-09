// SPDX-License-Identifier: Apache-2.0
// A put on a gate whose output net also has a weak driver: the slot holds
// strengths (nine values), and the delayed gate value reaches it as a strong
// one (IEEE 1800-2023 38.32, 28.16, 28.12). The plug-in puts rise 3, fall 5
// on gs at 10 ns. gs drives n9 strong; the weak continuous assignment
// (weak1, weak0) of 1 loses to it. a rises at 20 and falls at 40, so n9 is
// 0 at 22, 1 at 23 (20 + 3), 1 at 44 and 0 at 45 (40 + 5).
`timescale 1ns / 1ns
module top;
  reg a;
  wire n9;
  buf gs (n9, a);
  assign (weak1, weak0) n9 = 1'b1;
  initial begin
    a = 0;
    #20 a = 1;
    #2 $strobe("#c 22 n9=%b", n9);
    #1 $strobe("#c 23 n9=%b", n9);
    #17 a = 0;
    #4 $strobe("#c 44 n9=%b", n9);
    #1 $strobe("#c 45 n9=%b", n9);
    #5 $finish;
  end
endmodule
