// SPDX-License-Identifier: Apache-2.0
// INTERCONNECT delays of a net whose drivers of one source differ in drive
// strength (IEEE 1364-2005 16.2.4, IEEE 1800-2023 28.12): each strength of a
// source reaches the load with its own strength, and the load resolves them.
//
// Scenario 1, w: u6 drives w with a strong driver (enabled by ea, value a)
// and a weak driver (value b, always on); u7 drives w strong (enabled by e7,
// value d7). INTERCONNECT u6.z -> u8.a is 2 ns, u7.z -> u8.a is 5 ns.
// Before 20 ns w is the weak 0 of b. The probe u8 prints each change of the
// load from 10 ns on (a change is the resolution of the drivers as they
// arrive at the load):
//   20 ns  b=1: weak 1 alone; w=1, the load sees it at 22.   TRACE a=1 t=22
//   40 ns  ea=1 (a=0): strong 0 beats weak 1; w=0, load 42. TRACE a=0 t=42
//   60 ns  u7 drives strong 1: strong 0 meets strong 1, w=x. The strong 0 of
//          u6 is at the load, u7's 1 arrives at 65.         TRACE a=x t=65
//   80 ns  ea=0: u6's strong driver is off, weak 1 remains;
//          the load resolves weak 1 and strong 1 to 1 at 82. TRACE a=1 t=82
//  100 ns  b=0: u6's weak 0 arrives at 102, and loses to u7's strong 1: no
//          change at the load (a weak 0 driven as strong would make x).
//  120 ns  e7=0: u7 is off; weak 0 alone reaches the load at 125.
//                                                            TRACE a=0 t=125
//
// Scenario 2, the strength of the copy: one weak driver u9 (INTERCONNECT
// u9.z -> uv.a is 4 ns). The probe prints the strength and value (%v) of its
// port, which the delayed copy drives with the strength of the driver:
//  200 ns  aw=1: weak 1 reaches the load at 204.             We1
//  220 ns  aw=0: weak 0 reaches the load at 224.             We0
`timescale 1ns/1ps
module strong_weak(input logic ea, input logic a, input logic b,
    output wire z);
  assign z = ea ? a : 1'bz;
  assign (weak0, weak1) z = b;
endmodule

module tdrv(input logic a, input logic en, output tri z);
  assign z = en ? a : 1'bz;
endmodule

module wdrv(input logic a, output wire z);
  assign (weak0, weak1) z = a;
endmodule

module probe(input wire a);
  always @(a) if ($time >= 10) $display("TRACE a=%b t=%0d", a, $time);
endmodule

module probe_v(input wire a);
  always @(a) if ($time >= 150) $display("TRACE v=%v t=%0d", a, $time);
endmodule

module top;
  logic ea = 0, a = 0, b = 0, e7 = 0, d7 = 0;
  wire w;
  strong_weak u6(.ea(ea), .a(a), .b(b), .z(w));
  tdrv u7(.a(d7), .en(e7), .z(w));
  probe u8(.a(w));
  initial begin
    #20 b = 1;
    #20 ea = 1;
    #20 begin d7 = 1; e7 = 1; end
    #20 ea = 0;
    #20 b = 0;
    #20 e7 = 0;
  end
  logic aw = 0;
  wire wv;
  wdrv u9(.a(aw), .z(wv));
  probe_v uv(.a(wv));
  initial begin
    #200 aw = 1;
    #20 aw = 0;
  end
endmodule
