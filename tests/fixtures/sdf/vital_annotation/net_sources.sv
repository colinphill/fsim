// SPDX-License-Identifier: Apache-2.0
// INTERCONNECT delays between source ports whose drivers are not processes
// of the source instance that write the net (IEEE 1364-2005 16.2.4): a
// primitive, a VHDL entity, a nested instance, a constant, a pull-up, an
// output port connected to a select of a vector (the load too), and a source
// whose port is not on the net.
//
// Every scenario has a source A (INTERCONNECT delay 2 ns) and a source B (5 ns)
// of one net, and a probe on the net that prints each change of its port from
// 10 ns on. With T the scenario's start (20, 120, ... 620 ns):
//   T      A drives 1: the load sees it at T+2.           a=1 t=T+2
//   T+20   B drives 0 against A's 1: x, seen at T+25.     a=x t=T+25
//   T+40   A is off: B's 0 alone, seen at T+42.           a=0 t=T+42
//   T+60   B is off: z, seen at T+65.                     a=z t=T+65
// A constant A (scenario D, 1) or a pull-up A (scenario E, pull 1) is on from
// the start (its 1 reaches the load before 10 ns) and stays:
//   T+20   B drives 0: against the constant 1 it is x (D), against the weak
//          pull-up 1 it is 0 (E), seen at T+25.        D: a=x  E: a=0
//   T+40   B is off: 1 again, seen at T+45.            a=1
// Scenario G is a source not on the net: u7 drives w7b[1], the net w7 is
// assigned from it in the parent, so the INTERCONNECT u7.z -> pG.a names a
// port that is not on the load's net. The delay is annotated anyway, as the
// delay from all sources (16.2.4), 2 ns, and the INTERCONNECT v7.z -> pG.a
// after it sets the 5 ns of v7: the same trace as for scenarios A to C and F.
`timescale 1ns/1ps
module tdrv(input logic a, input logic en, output tri z);
  assign z = en ? a : 1'bz;
endmodule

module prim(input logic a, input logic en, output tri z);
  bufif1 g(z, a, en);
endmodule

module wrap(input logic a, input logic en, output tri z);
  tdrv c(.a(a), .en(en), .z(z));
endmodule

module cst(output wire z);
  assign z = 1'b1;
endmodule

module pul(output wire z);
  pullup (z);
endmodule

module probe #(parameter string N = "p") (input wire a);
  always @(a) if ($time >= 10) $display("TRACE %s a=%b t=%0d", N, a, $time);
endmodule

module top;
  // Scenario A: a primitive source.
  logic ea = 0, da = 0, eb = 0, db = 0;
  wire wa;
  prim ua(.a(da), .en(ea), .z(wa));
  tdrv va(.a(db), .en(eb), .z(wa));
  probe #("A") pa(.a(wa));
  initial begin
    #20 begin da = 1; ea = 1; end
    #20 eb = 1;
    #20 ea = 0;
    #20 eb = 0;
  end

  // Scenario B: output ports connected to a select, and the load too.
  logic ea2 = 0, da2 = 0, eb2 = 0, db2 = 0;
  wire [1:0] wb;
  tdrv ub(.a(da2), .en(ea2), .z(wb[0]));
  tdrv vb(.a(db2), .en(eb2), .z(wb[0]));
  probe #("B") pb(.a(wb[0]));
  initial begin
    #120 begin da2 = 1; ea2 = 1; end
    #20 eb2 = 1;
    #20 ea2 = 0;
    #20 eb2 = 0;
  end

  // Scenario C: a VHDL source.
  logic ea3 = 0, da3 = 0, eb3 = 0, db3 = 0;
  wire wc;
  vdrv uc(.a(da3), .en(ea3), .z(wc));
  tdrv vc(.a(db3), .en(eb3), .z(wc));
  probe #("C") pc(.a(wc));
  initial begin
    #220 begin da3 = 1; ea3 = 1; end
    #20 eb3 = 1;
    #20 ea3 = 0;
    #20 eb3 = 0;
  end

  // Scenario D: a constant source.
  logic eb4 = 0, db4 = 0;
  wire wd;
  cst ud(.z(wd));
  tdrv vd(.a(db4), .en(eb4), .z(wd));
  probe #("D") pd(.a(wd));
  initial begin
    #340 eb4 = 1;
    #20 eb4 = 0;
  end

  // Scenario E: a pull-up source.
  logic eb5 = 0, db5 = 0;
  wire we;
  pul ue(.z(we));
  tdrv ve(.a(db5), .en(eb5), .z(we));
  probe #("E") pe(.a(we));
  initial begin
    #440 eb5 = 1;
    #20 eb5 = 0;
  end

  // Scenario F: a nested source.
  logic ea6 = 0, da6 = 0, eb6 = 0, db6 = 0;
  wire wf;
  wrap uf(.a(da6), .en(ea6), .z(wf));
  tdrv vf(.a(db6), .en(eb6), .z(wf));
  probe #("F") pf(.a(wf));
  initial begin
    #520 begin da6 = 1; ea6 = 1; end
    #20 eb6 = 1;
    #20 ea6 = 0;
    #20 eb6 = 0;
  end

  // Scenario G: a source that is not on the net.
  logic ea7 = 0, da7 = 0, eb7 = 0, db7 = 0;
  wire [1:0] wg_b;
  wire wg;
  tdrv ug(.a(da7), .en(ea7), .z(wg_b[1]));
  assign wg = wg_b[1];
  tdrv vg(.a(db7), .en(eb7), .z(wg));
  probe #("G") pg(.a(wg));
  initial begin
    #620 begin da7 = 1; ea7 = 1; end
    #20 eb7 = 1;
    #20 ea7 = 0;
    #20 eb7 = 0;
  end
endmodule
