// SPDX-License-Identifier: Apache-2.0
// A design annotated with an SDF INTERCONNECT and a PORT delay, for the VPI
// intermodule path and delay device objects (IEEE 1800-2023 37.37, 37.45).
// The delay also delays the net: u1 sees the value of w that u0 drives after
// the delay, 6 ns to rise and 7 ns to fall once the plug-in has written the
// intermodule path (vpi_put_delays, 38.32), while u0 sees a at once. The
// plug-in reads the load port u1.a (and its low connection) late and the
// source side (u0.z, w and the high connection) at once.
`timescale 1ns/1ps
module drv(input logic a, output logic z);
  assign z = a;
  always @(a) if ($time >= 20) $display("#c net %m a=%b t=%0d", a, $time);
endmodule

module bus #(parameter bit SHOW = 0)
    (input logic [3:0] a, output logic [3:0] z);
  assign z = a;
  always @(a) if (SHOW && $time >= 20) $display("#c net %m a=%b t=%0d", a, $time);
endmodule

module tdrv(input logic a, input logic en, output tri z);
  assign z = en ? a : 1'bz;
endmodule

// A driver that prints nothing, for the net whose delayed copy a probe shows.
module src(input logic a, output wire z);
  assign z = a;
endmodule

module probe(input wire a);
  always @(a) if ($time >= 20) $display("#c net %m a=%b t=%0d", a, $time);
endmodule

module quiet(input logic a, output logic z);
  assign z = a;
endmodule

// A port of the design itself, which the plug-in drives: its PORT delay
// delays what the instances below read (IEEE 1364-2005 16.2.4).
module top(input wire tin);
  logic a = 1'b1;
  wire w, y;
  drv u0(.a(a), .z(w));
  drv u1(.a(w), .z(y));
  // A PORT delay then an INTERCONNECT delay to the same load: the later
  // record applies (IEEE 1364-2005 16.3), 2 ns to rise and 3 ns to fall.
  logic b = 1'b1;
  wire w2, y2;
  drv u2(.a(b), .z(w2));
  drv u3(.a(w2), .z(y2));
  initial begin
    #25 b = 1'b0;
    #20 b = 1'b1;
  end
  // Delays on selects of a vector port: an INTERCONNECT to bit 1 (2 ns to
  // rise, 3 ns to fall) and a PORT delay on bits 3:2 (5 ns); bit 0 has
  // none, so u5 sees it at once.
  logic [3:0] c = 4'b0000;
  wire [3:0] w3, y3;
  bus u4(.a(c), .z(w3));
  bus #(.SHOW(1)) u5(.a(w3), .z(y3));
  initial #50 c = 4'b1111;
  // Two sources of one net with different INTERCONNECT delays to the same
  // load (IEEE 1364-2005 16.2.4): u8 sees what u6 drives 2 ns later and what
  // u7 drives 7 ns later, the net resolving both.
  logic d6 = 1'b0, e6 = 1'b0, d7 = 1'b0, e7 = 1'b0;
  wire w4;
  tdrv u6(.a(d6), .en(e6), .z(w4));
  tdrv u7(.a(d7), .en(e7), .z(w4));
  probe u8(.a(w4));
  initial begin
    #21 e6 = 1'b1;
    #10 begin e6 = 1'b0; e7 = 1'b1; d7 = 1'b1; end
    #10 d7 = 1'b0;
  end
  // The plug-in reads both sides of an INTERCONNECT delay (3 ns to rise,
  // 4 ns to fall) at every point of the delay: the source u10.z at once, the
  // load u11.a and the changes it reports late.
  logic g = 1'b1;
  wire w5, y5;
  quiet u10(.a(g), .z(w5));
  quiet u11(.a(w5), .z(y5));
  initial begin
    #9 g = 1'b0;
    #17 g = 1'b1;
  end
  // A hierarchical name of a load port reads the delayed value wherever it
  // is written (IEEE 1364-2005 16.2.4): top names u1.a and u13.a from outside
  // their instances, between the change of the source and its arrival at the
  // load (u1.a 7 ns after w falls, u13.a 6 ns after w6 falls and 5 ns after
  // it rises), in a statement and in an implicit event list.
  initial begin
    #22 $display("#c hier top.u1.a=%b u1.a=%b w=%b t=%0d", top.u1.a, u1.a, w, $time);
    #21 $display("#c hier top.u1.a=%b u1.a=%b w=%b t=%0d", top.u1.a, u1.a, w, $time);
  end
  logic h = 1'b1;
  wire w6, y6;
  quiet u12(.a(h), .z(w6));
  quiet u13(.a(w6), .z(y6));
  initial begin
    #12 h = 1'b0;
    #22 h = 1'b1;
  end
  initial begin
    #16 $display("#c hier top.u13.a=%b u13.a=%b w6=%b t=%0d", top.u13.a, u13.a, w6, $time);
    #20 $display("#c hier top.u13.a=%b u13.a=%b w6=%b t=%0d", top.u13.a, u13.a, w6, $time);
  end
  always @* if ($time >= 10) $display("#c star u13.a=%b t=%0d", u13.a, $time);
  // The PORT delay of the design's port tin is 3 ns to rise and 5 ns to
  // fall: u9 sees what the plug-in drives at 56 ns and 62 ns at 59 ns and
  // 67 ns, while the port itself shows it at once.
  probe u9(.a(tin));
  // An INTERCONNECT delay of twelve transition delays (IEEE 1364-2005
  // 16.2.4, SDF order 0->1, 1->0, 0->z, z->1, 1->z, z->0, 0->x, x->1, 1->x,
  // x->0, x->z, z->x: 2 3 4 5 6 7 8 9 10 11 12 13): each change of the net
  // reaches uxp after the delay of its own transition, and the changes are
  // spaced further apart than any delay. uxp prints a=1 at 132, z at 146,
  // 1 at 165, 0 at 173, z at 184, 0 at 207, x at 218, 1 at 239, x at 260,
  // 0 at 281, x at 298, z at 312 and x at 333.
  logic dx = 1'b0, ex = 1'b1;
  wire wx;
  tdrv ux(.a(dx), .en(ex), .z(wx));
  probe uxp(.a(wx));
  initial begin
    #130 dx = 1'b1;   // 0->1 after 2
    #10 ex = 1'b0;    // 1->z after 6
    #20 ex = 1'b1;    // z->1 after 5
    #10 dx = 1'b0;    // 1->0 after 3
    #10 ex = 1'b0;    // 0->z after 4
    #20 ex = 1'b1;    // z->0 after 7
    #10 dx = 1'bx;    // 0->x after 8
    #20 dx = 1'b1;    // x->1 after 9
    #20 dx = 1'bx;    // 1->x after 10
    #20 dx = 1'b0;    // x->0 after 11
    #20 dx = 1'bx;    // 0->x after 8
    #10 ex = 1'b0;    // x->z after 12
    #20 ex = 1'b1;    // z->x after 13
  end
  // Pulse limits (a delval of delay, reject and error limit): a rise takes
  // 10 with reject limit 3 and error limit 6, a fall 8 with limits 2 and 5.
  // x follows from the rule that fills the twelve transitions: 0->x 8, x->1
  // 10, 1->x 8, x->0 8, with the limits of those. A change after a pending
  // one finds the pulse between them as wide as the time since the pending
  // change was detected. upp prints:
  //  - 300 rise, 302 fall: the pulse is 2 wide, below the reject limit 3, so
  //    it is dropped and nothing prints;
  //  - 320 rise, 324 fall: 4 wide, from the reject limit 3 up to the error
  //    limit 6, so the rise becomes x at its time 330 and the fall follows
  //    from x with the x->0 delay 8: x at 330, 0 at 332;
  //  - 350 rise, 357 fall: 7 wide, past the error limit, so the rise reaches
  //    the load at 360 and the fall, still pending then, 8 after 357: 1 at
  //    360, 0 at 365;
  //  - 380 rise, 388 fall, 394 rise: the changes are transported, three
  //    pending at once (the fall at 396 and the rise at 404 while the rise
  //    at 390 has matured): 1 at 390, 0 at 396, 1 at 404;
  //  - 440 fall, 442 rise: 2 wide, the reject limit of a fall (a pulse of
  //    that width after a rise is dropped, as at 300) and below its error
  //    limit 5, so the fall becomes x at 448 and the rise follows with the
  //    x->1 delay 10: x at 448, 1 at 452.
  logic dp = 1'b0;
  wire wp;
  src up(.a(dp), .z(wp));
  probe upp(.a(wp));
  initial begin
    #300 dp = 1'b1;
    #2 dp = 1'b0;
    #18 dp = 1'b1;
    #4 dp = 1'b0;
    #26 dp = 1'b1;
    #7 dp = 1'b0;
    #23 dp = 1'b1;
    #8 dp = 1'b0;
    #6 dp = 1'b1;
    #46 dp = 1'b0;
    #2 dp = 1'b1;
  end
  initial begin
    #20 a = 1'b0;
    #20 a = 1'b1;
    #450 $finish;
  end
endmodule
