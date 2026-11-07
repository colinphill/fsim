// SPDX-License-Identifier: Apache-2.0
// INTERCONNECT and PORT delays on nets with bidirectional switches.
// IEEE 1364-2005 16.2.4 defines an interconnect delay as "the signal
// propagation delay from an output or inout module port to an input or inout
// module port", annotated "only between module ports, never between
// primitive pins". A tran of the design is a primitive: its terminals are
// pins, and the clause gives no source and load across it, no direction and
// no delay.
//
// Scenario 1, a switch inside the load: the INTERCONNECT u0.z -> p.a (4 ns)
// ends at the inout port a of sw, whose tran joins a and b. No delay is
// defined across the tran, so the net is not delayed and annotation warns
// (FSIM-SDF-SESSION-004, "defines no delay across a switch"). The probe pb
// on b shows the value of w at once:
//   20 ns  u0 drives 1; b follows at once.                 TRACE sw b=1 t=20
//   40 ns  u0 is off; z at once.                           TRACE sw b=z t=40
//
// Scenario 2, a switch outside the load: the PORT delay of pt.a (4 ns) is the
// delay from all sources on the net. The tran joins the net w2 to y, a
// source on the net is the one driving y through the tran, and the load reads
// the value of w2, whatever put it there, 4 ns later:
//  120 ns  ub drives y with 1; w2 follows through the tran at once, the
//          load sees it at 124.                            TRACE tr a=1 t=124
//  140 ns  ub is off; z reaches the load at 144.           TRACE tr a=z t=144
`timescale 1ns/1ps
module tdrv(input logic a, input logic en, output tri z);
  assign z = en ? a : 1'bz;
endmodule

module sw(inout wire a, inout wire b);
  tran t(a, b);
endmodule

module probe #(parameter string N = "p") (input wire a);
  always @(a) if ($time >= 10) $display("TRACE %s a=%b t=%0d", N, a, $time);
endmodule

module top;
  logic a0 = 0, e0 = 0;
  wire w, x;
  tdrv u0(.a(a0), .en(e0), .z(w));
  sw p(.a(w), .b(x));
  always @(x) if ($time >= 10 && $time < 100) $display("TRACE sw b=%b t=%0d", x, $time);
  initial begin
    #20 begin a0 = 1; e0 = 1; end
    #20 e0 = 0;
  end

  logic ab = 0, eb = 0;
  wire w2, y;
  tdrv ub(.a(ab), .en(eb), .z(y));
  tran t2(w2, y);
  probe #("tr") pt(.a(w2));
  initial begin
    #120 begin ab = 1; eb = 1; end
    #20 eb = 0;
  end
endmodule
