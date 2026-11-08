// SPDX-License-Identifier: Apache-2.0
// Module path delays written through specparams that are minimum:typical:
// maximum triples (IEEE 1800-2023 30.4.1, 38.10 mtm_flag) and the delay
// devices of module paths (37.45, 37.46), plus a put on a tranif's turn-on
// and turn-off delays (28.16).
`timescale 1ns / 1ns
module dut (
    input  wire a,
    input  wire c,
    input  wire e,
    output wire y,
    output wire z,
    output wire w
);
  specparam tpd = 2:4:6;
  specparam lo = 1:2:3, hi = 4:5:6;
  specparam twice = tpd * 2;  // computed from a triple: 4:8:12
  assign y = a;
  assign z = c;
  assign w = e;
  specify
    (a => y) = (tpd);
    (c => z) = (lo, hi);
    (e => w) = (twice);
  endspecify
endmodule

module top;
  reg a, c, e, en;
  wire y, z, w, t1, t0, bus;
  reg drive;
  assign bus = drive;
  dut u (.a(a), .c(c), .e(e), .y(y), .z(z), .w(w));
  tranif1 #(3, 6) x1 (bus, t1, en);
  tranif0 #(3, 6) x0 (bus, t0, en);
  initial begin
    a = 0; c = 0; e = 0; en = 0; drive = 1;
    // x1 is tranif1 #(3, 6) and the plug-in puts (3, 7): the control of x1
    // turns it on 3 ns after it rises and off 7 ns after it falls. x0 is a
    // tranif0 #(3, 6): it turns on 3 ns after the control falls and off 6 ns
    // after it rises. en rises at 10 and falls at 50, so x1 is on from 13 to
    // 57 and x0 is off from 16 to 53 (and on before 16 is not observed).
    #10 en = 1;
    #40 en = 0;
    #30 $finish;
  end
  // $strobe reports the values at the end of its time step.
  initial begin
    #19 $strobe("#c 19 t1=%b t0=%b", t1, t0);
    #33 $strobe("#c 52 t1=%b t0=%b", t1, t0);
    #1 $strobe("#c 53 t1=%b t0=%b", t1, t0);
    #3 $strobe("#c 56 t1=%b t0=%b", t1, t0);
    #2 $strobe("#c 58 t1=%b t0=%b", t1, t0);
  end
endmodule
