// SPDX-License-Identifier: Apache-2.0
//
// An expect statement's property with an asynchronous abort, and with a
// disable condition that reads a variable of the procedure (IEEE 1800-2023
// 16.17, 16.12.14, 16.12). The statement's monitor evaluates at every time
// step of its attempt where the abort condition's sampled value or the
// disable condition may have changed, not only at the clock's ticks, and
// the statement completes in the time step its attempt ends (16.17).
//
// c1 rises at 10, 30, 50, ...; b stays 0.
//   e1  from 25: expect (@(posedge c1) accept_on(d) (1 ##1 b)). The attempt
//       starts at 30 and would fail at 50. d is 1 from 40 to 41, so its
//       sampled value is 1 in the time step 41: the attempt is accepted
//       there and the statement completes at 41.
//   e2  from 105: expect (@(posedge c1) disable iff (loc) (1 ##1 b)), loc a
//       variable of the procedure's block (a continuous assignment of the
//       scope cannot read it). The attempt starts at 110 and would fail at
//       130. loc is 1 from 120 to 125: the attempt is disabled at 120 (no
//       action runs) and the statement completes there.
//   e3  from 145: expect (@(posedge c1) reject_on(r) (1 ##1 1)). The
//       attempt starts at 150 and would pass at 170. r is 1 from 160 to
//       161: the attempt is rejected at 161 and the statement completes
//       there.
module top;
  bit c1;
  logic b = 0, d = 0, r = 0;
  always #10 c1 = ~c1;
  initial begin
    #40 d = 1;
    #1 d = 0;    // 41
    #119 r = 1;  // 160
    #1 r = 0;    // 161
  end
  initial begin
    #25;
    expect (@(posedge c1) accept_on(d) (1 ##1 b))
      $display("inl e1 pass t=%0t", $time);
    else $display("inl e1 fail t=%0t", $time);
    $display("inl e1 done t=%0t", $time);
  end
  initial begin
    bit loc;
    loc = 0;
    #105;
    fork
      begin
        #15 loc = 1;  // 120
        #5 loc = 0;   // 125
      end
    join_none
    expect (@(posedge c1) disable iff (loc) (1 ##1 b))
      $display("inl e2 pass t=%0t", $time);
    else $display("inl e2 fail t=%0t", $time);
    $display("inl e2 done t=%0t", $time);
  end
  initial begin
    #145;
    expect (@(posedge c1) reject_on(r) (1 ##1 1))
      $display("inl e3 pass t=%0t", $time);
    else $display("inl e3 fail t=%0t", $time);
    $display("inl e3 done t=%0t", $time);
  end
  initial #200 $finish;
endmodule
