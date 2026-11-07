// SPDX-License-Identifier: Apache-2.0
// A foreign write of a net whose load has per-source INTERCONNECT delays
// (SDF back annotation, IEEE 1364-2005 16.2.4; vpi_put_value, IEEE 1800-2023
// 38.34). Each net has two sources, A and B, that stay off until the end, so
// the plug-in's writes are the only changes of the net until then. A write
// no source made reaches the load copy after the smallest of the two
// sources' delays for each bit's transition (as IEEE 1076.4-2000 6.4.1 treats
// a change no source made): a deposit as a deposit of the copy, a force as a
// force of the copy, a release as a release of the copy. A later write that
// matures at or before a pending one ends it (transport), and pulse limits
// do not apply. The twelve delays of the SDF, in the order 0->1 1->0 0->z
// z->1 1->z z->0 0->x x->1 1->x x->0 x->z z->x:
//   A  2 3 2 4 2 6 2 2 2 2 2 2        B  5 5 5 3 5 2 5 5 5 5 5 5
// so the smallest of the two is  0->1 2, 1->0 3, 0->z 2, z->1 3, 1->z 2,
// z->0 2, and A alone gives z->1 4 and B alone z->0 2. The plug-in acts at
// the times below (ns) and the probes of the loads print each change of
// their port. No two lines of the trace share a time.
//
// Net D, a deposit (the probe pd; the load's copy starts as z):
//   t=20  deposit 1, z->1:                              3    D a=1 t=23
//   t=40  deposit 0, 1->0:                              3    D a=0 t=43
//   t=100 deposit 1, 0->1:                              2    D a=1 t=102
//   t=101 deposit 0, from the pending 1, 1->0:          3    D a=0 t=104
//         (the pulse of 2 ns passes: no pulse limit)
//   t=110 deposit 1, 0->1:                              2    D a=1 t=112
//   t=140 deposit 0, 1->0:                              3    to 143
//   t=141 deposit 1, from the pending 0, 0->1:          2    to 143
//         (it matures at the pending write's time and ends it: the copy
//         stays 1, nothing is printed)
//   t=200 deposit 0, 1->0:                              3    D a=0 t=203
//   t=300 source A drives 1: the copy drivers re-resolve the copy to A's 1
//         after A's own z->1 delay of 4 (not the smallest):     D a=1 t=304
// Net F, a force and its release (the probe pf):
//   t=25  force 1, z->1:                                3    F a=1 t=28
//   t=45  force 0, 1->0:                                3    F a=0 t=48
//   t=65  release; the net returns to z, 0->z:          2    F a=z t=67
//         (the copy is released and returns to its drivers' z)
//   t=310 source B drives 0, B's own z->0 of 2:               F a=0 t=312
// Net R, a deposit, then a force and its release of the same net (the probe
// pr): the release starts from the forced value and ends at the net's
// drivers' z, neither at the deposited value (1->0 would take 3) nor at the
// forced one (no change, no delay):
//   t=70  deposit 0, z->0:                              2    R a=0 t=72
//   t=80  force 1, 0->1:                                2    R a=1 t=82
//   t=90  release; the net returns to z, 1->z:          2    R a=z t=92
// Net V, a 2-bit vector whose bits make different transitions (the probe pv):
//   t=30  deposit 2'b01: bit 1 z->0 takes 2, bit 0 z->1 takes 3
//                                         V a=0z t=32, V a=01 t=33
//   t=50  deposit 2'b10: bit 1 0->1 takes 2, bit 0 1->0 takes 3
//                                         V a=11 t=52, V a=10 t=53
//   t=320 source A drives 2'b11, both bits z->1 of A's 4:     V a=11 t=324
`timescale 1ns/1ps
module bdrv(input logic [1:0] a, input logic en, output tri [1:0] z);
  assign z = en ? a : 2'bzz;
endmodule

module tdrv(input logic a, input logic en, output tri z);
  assign z = en ? a : 1'bz;
endmodule

module probe #(parameter string N = "p") (input wire a);
  always @(a) if ($time >= 10) $display("#c %s a=%b t=%0d", N, a, $time);
endmodule

module probev #(parameter string N = "p") (input wire [1:0] a);
  always @(a) if ($time >= 10) $display("#c %s a=%b t=%0d", N, a, $time);
endmodule

module top;
  logic ead = 0, dad = 0, ebd = 0, dbd = 0;
  wire wd;
  tdrv uda(.a(dad), .en(ead), .z(wd));
  tdrv udb(.a(dbd), .en(ebd), .z(wd));
  probe #("D") pd(.a(wd));

  logic eaf = 0, daf = 0, ebf = 0, dbf = 0;
  wire wf;
  tdrv ufa(.a(daf), .en(eaf), .z(wf));
  tdrv ufb(.a(dbf), .en(ebf), .z(wf));
  probe #("F") pf(.a(wf));

  logic ear = 0, dar = 0, ebr = 0, dbr = 0;
  wire wr;
  tdrv ura(.a(dar), .en(ear), .z(wr));
  tdrv urb(.a(dbr), .en(ebr), .z(wr));
  probe #("R") pr(.a(wr));

  logic eav = 0, ebv = 0;
  logic [1:0] dav = 2'b00, dbv = 2'b00;
  wire [1:0] wv;
  bdrv uva(.a(dav), .en(eav), .z(wv));
  bdrv uvb(.a(dbv), .en(ebv), .z(wv));
  probev #("V") pv(.a(wv));

  initial begin
    #300 begin dad = 1; ead = 1; end
    #10 begin dbf = 0; ebf = 1; end
    #10 begin dav = 2'b11; eav = 1; end
    #80 $finish;
  end
endmodule
