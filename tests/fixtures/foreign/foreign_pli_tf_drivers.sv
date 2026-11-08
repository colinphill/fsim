// SPDX-License-Identifier: Apache-2.0
// reason_paramdrc for the drivers a force, a procedural continuous assign and
// a concatenation target make (IEEE 1364-2001 21.3.2, 9.3, Table 88). Each
// module has its own time window so the trace is in time order.
`timescale 1ns / 1ps
module top;
endmodule

// A force is a driver of its variable: it changes value when it starts, when
// a force of another value replaces it and when the release ends it; a force
// of the value it already contributes changes nothing.
//   t=2 tf_asynchon, t=5 force 1 (change 1), t=8 force 1 (none),
//   t=11 force 0 (change 2), t=14 release (change 3).
module drcf;
  reg r;
  initial begin
    r = 0;
    #2 $pli_tf_drc(r);
    #3 force r = 1;
    #3 force r = 1;
    #3 force r = 0;
    #3 release r;
  end
endmodule

// A procedural continuous assign is a driver whose value follows its
// right-hand side while it is in effect.
//   t=102 tf_asynchon, t=105 assign (change 4), t=108 s=1 so the driver's
//   value is 1 (change 5), t=111 deassign (change 6), t=112 s=0 (none).
module drca;
  reg r, s;
  initial begin
    r = 0; s = 0;
    #102 $pli_tf_drc(r);
    #3 assign r = s;
    #3 s = 1;
    #3 deassign r;
    #1 s = 0;
  end
endmodule

// The driver a concatenation target makes of a is bit 1 of e: it changes at
// t=205 (e=10, a=1) and t=211 (e=00, a=0) but not at t=208 (e=11).
module drcc;
  reg [1:0] e;
  wire a, b;
  assign {a, b} = e;
  initial begin
    e = 0;
    #202 $pli_tf_drc(a);
    #3 e = 2'b10;
    #3 e = 2'b11;
    #3 e = 2'b00;
  end
endmodule

// A net with several drivers, two gates and a continuous assignment: Table 88
// says reason_paramdrc is "a value change on the driver" of an argument, so
// each driver's own change is one call, also when the net's value does not
// change (the other drivers keep it at x) or the driver's change is hidden by
// another driver. All three drive 0 from the start (the gates and the
// assignment evaluate at time 0, before the watch).
//   t=302 tf_asynchon, t=305 p=1 (g1 0 -> 1: change 9, the net is x),
//   t=308 q=1 (g2 0 -> 1: change 10, the net stays x), t=311 s=1 (the
//   assignment 0 -> 1: change 11, the net becomes 1), t=314 p=1 (g1 keeps 1:
//   none), t=317 p=0 (g1 1 -> 0: change 12, the net stays x).
module drcm;
  reg p, q, s;
  wire n;
  buf g1(n, p);
  buf g2(n, q);
  assign n = s;
  initial begin
    p = 0; q = 0; s = 0;
    #302 $pli_tf_drc(n);
    #3 p = 1;
    #3 q = 1;
    #3 s = 1;
    #3 p = 1;
    #3 p = 0;
  end
endmodule
