// SPDX-License-Identifier: Apache-2.0
//
// Asynchronous aborts and a sampled disable condition of multiclocked
// properties (IEEE 1800-2023 16.13, 16.12.14, 16.12, 16.9.3). Each clock's
// process sets a tick bit of its own, which the monitor reads with its
// current value, as the single clock does: the abort conditions are checked
// with sampled values at every time step of an attempt and the disable
// condition at every time step of an attempt too, in generated code, with no
// event's triggered state read in the tick.
//
// Clocks: c1 rises at 10, 30, 50, 70, 90, 110; c2 at 15, 35, 55, 75, 95.
// An enable sampled 1 at a c1 tick starts the consequent there; its first
// boolean is checked at the next c2 tick and its last at the c1 tick after
// that. Ticks where the enable is sampled 0 are vacuous passes (no output).
//   m1  a1 |-> accept_on(d) (@(posedge c2) b ##1 @(posedge c1) b), b is 0:
//       the attempt from 30 would fail at 35. d is 1 from 32 to 33, so its
//       sampled value is 1 in the time step 33: the attempt is accepted
//       there and its pass statement runs.
//   m2  the same with accept_on(e), e never 1: fails at 35.
//   m3  a2 |-> reject_on(r) (@(posedge c2) 1 ##1 @(posedge c1) 1): the
//       attempt from 70 would pass at 90; r is 1 from 82 to 83, so it is
//       rejected at 83.
//   m4  disable iff ($rose(s, @(posedge c1))) a1 |-> @(posedge c2) 1 ##1
//       @(posedge c1) 0 (a value change function in a disable condition
//       names its clock, 16.12): the attempt from 30 would fail at 50. s is
//       1 from 42 to 47: in the time step 45 (c2 falls) $rose compares s's
//       sampled value 1 with the one at the c1 tick 30, 0, so the attempt is
//       disabled at 45 (no outcome). At the tick 50 s is sampled 0 again: a
//       reading at the ticks only would see the attempt fail at 50.
module top;
  bit c1, c2;
  logic a1 = 0, a2 = 0, b = 0, d = 0, e = 0, r = 0, s = 0;
  always #10 c1 = ~c1;
  initial begin
    #15;
    forever begin
      c2 = 1; #10; c2 = 0; #10;
    end
  end
  initial begin
    #25 a1 = 1;
    #10 a1 = 0;  // 35
  end
  initial begin
    #65 a2 = 1;
    #10 a2 = 0;  // 75
  end
  initial begin
    #32 d = 1;
    #1 d = 0;    // 33
    #9 s = 1;    // 42
    #5 s = 0;    // 47
    #35 r = 1;   // 82
    #1 r = 0;    // 83
  end

  m1: assert property (@(posedge c1) a1 |-> accept_on(d)
      (@(posedge c2) b ##1 @(posedge c1) b))
    begin if (a1) $display("inl m1 pass t=%0t", $time); end
    else $display("inl m1 fail t=%0t", $time);
  m2: assert property (@(posedge c1) a1 |-> accept_on(e)
      (@(posedge c2) b ##1 @(posedge c1) b))
    else $display("inl m2 fail t=%0t", $time);
  m3: assert property (@(posedge c1) a2 |-> reject_on(r)
      (@(posedge c2) 1 ##1 @(posedge c1) 1))
    else $display("inl m3 fail t=%0t", $time);
  m4: assert property (@(posedge c1) disable iff ($rose(s, @(posedge c1))) a1 |->
      @(posedge c2) 1 ##1 @(posedge c1) 0)
    else $display("inl m4 fail t=%0t", $time);
  initial #120 $finish;
endmodule
