// SPDX-License-Identifier: Apache-2.0
// INTERCONNECT delays to an inout port whose instance drives the net itself
// (IEEE 1364-2005 16.2.4): the delay is the one from the other source port to
// the load, and the load's own drive reaches the net, and the load's own
// reads, at once. Each load reads what its source drove 3 ns ago resolved
// with what the load drives now.
//
// Scenario 1 (pad p1 on w; u0 drives w, INTERCONNECT u0.z -> p1.io is 3 ns;
// the pad prints each change of io from 10 ns on):
//   20 ns  u0 drives 1; the pad sees it at 23.            TRACE pad io=1 t=23
//   40 ns  the pad drives 0 against u0's 1: the pad sees x at once, w is x.
//                                                         TRACE pad io=x t=40
//   60 ns  u0 is off (the pad's 0 stays); its z arrives at the pad at 63.
//                                                         TRACE pad io=0 t=63
//   80 ns  the pad is off: z at once, since u0's z arrived long ago.
//                                                         TRACE pad io=z t=80
//  100 ns  u0 drives 0: 103.                              TRACE pad io=0 t=103
// The net itself is exact for its other reads: pw prints w 1 ns after each of
// its changes (w=1 at 20, x at 40, 0 at 60, z at 80, 0 at 100):
//   TRACE pw w=1 t=21, w=x t=41, w=0 t=61, w=z t=81, w=0 t=101.
//
// Scenario 2 (pad pv on w2; INTERCONNECT u2.z -> pv.io is 3 ns): the pad
// prints the strength of its port with %v, 200 ns later in the same pattern:
//  220 ns  u2 drives 1; the pad sees it at 223.      TRACE strength io=St1 t=223
//  240 ns  the pad drives 0: x at once.              TRACE strength io=StX t=240
//
// Scenario 3 (pad pm on w3; INTERCONNECT u3.z -> pm.io is 3 ns): the pad
// installs a $monitor of its port at 410 ns, which prints at once and at each
// change of the port.
//  410 ns  io is z.                                      TRACE mon io=z t=410
//  420 ns  u3 drives 1; the monitor sees it at 423.      TRACE mon io=1 t=423
//  440 ns  the pad drives 0: x at once.                  TRACE mon io=x t=440
`timescale 1ns/1ps
module tdrv(input logic a, input logic en, output tri z);
  assign z = en ? a : 1'bz;
endmodule

module pad(inout wire io, input logic en, input logic d);
  assign io = en ? d : 1'bz;
  always @(io) if ($time >= 10 && $time < 150)
    $display("TRACE pad io=%b t=%0d", io, $time);
endmodule

module padv(inout wire io, input logic en, input logic d);
  assign io = en ? d : 1'bz;
  always @(io) if ($time >= 150 && $time < 350)
    $display("TRACE strength io=%v t=%0d", io, $time);
endmodule

module padm(inout wire io, input logic en, input logic d);
  assign io = en ? d : 1'bz;
  initial begin
    #410 $monitor("TRACE mon io=%b t=%0d", io, $time);
  end
endmodule

module top;
  logic a0 = 0, e0 = 0, en1 = 0, d1 = 0;
  wire w;
  tdrv u0(.a(a0), .en(e0), .z(w));
  pad p1(.io(w), .en(en1), .d(d1));
  always @(w) if ($time >= 10 && $time < 150) begin
    #1 $display("TRACE pw w=%b t=%0d", w, $time);
  end
  initial begin
    #20 begin a0 = 1; e0 = 1; end
    #20 begin d1 = 0; en1 = 1; end
    #20 e0 = 0;
    #20 en1 = 0;
    #20 begin a0 = 0; e0 = 1; end
  end

  logic a2 = 0, e2 = 0, en2 = 0, d2 = 0;
  wire w2;
  tdrv u2(.a(a2), .en(e2), .z(w2));
  padv pv(.io(w2), .en(en2), .d(d2));
  initial begin
    #220 begin a2 = 1; e2 = 1; end
    #20 begin d2 = 0; en2 = 1; end
  end

  logic a3 = 0, e3 = 0, en3 = 0, d3 = 0;
  wire w3;
  tdrv u3(.a(a3), .en(e3), .z(w3));
  padm pm(.io(w3), .en(en3), .d(d3));
  initial begin
    #420 begin a3 = 1; e3 = 1; end
    #20 begin d3 = 0; en3 = 1; end
  end
endmodule
