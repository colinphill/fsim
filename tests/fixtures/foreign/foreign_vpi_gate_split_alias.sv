// SPDX-License-Identifier: Apache-2.0
// A put on gates that drive bits of a net alias family (IEEE 1800-2023
// 10.11, 28.16, 38.32): gl drives al[1], gl2 drives lq0, which is al[0]
// through the alias {lq1, lq0} = al[1:0], so the delayed write lands in the
// leaves of the vector alias. The plug-in puts rise 3, fall 5 on both at 10
// ns; a rises at 20 and falls at 40, so al[1:0] is 00 at 22, 11 at 23 (20 +
// 3), 11 at 44 and 00 at 45 (40 + 5). al[3:2] are undriven (z).
`timescale 1ns / 1ns
module top;
  reg a;
  wire lq0, lq1;
  wire [3:0] al;
  alias {lq1, lq0} = al[1:0];
  buf gl (al[1], a);
  buf gl2 (lq0, a);
  initial begin
    a = 0;
    #20 a = 1;
    #2 $strobe("#c 22 al=%b lq=%b%b", al, lq1, lq0);
    #1 $strobe("#c 23 al=%b lq=%b%b", al, lq1, lq0);
    #17 a = 0;
    #4 $strobe("#c 44 al=%b lq=%b%b", al, lq1, lq0);
    #1 $strobe("#c 45 al=%b lq=%b%b", al, lq1, lq0);
    #5 $finish;
  end
endmodule
