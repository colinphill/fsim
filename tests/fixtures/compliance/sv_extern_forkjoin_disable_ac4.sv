// fsim: top=top std=2017
// SPDX-License-Identifier: Apache-2.0
// The effect of a disable on an `extern forkjoin task` (IEEE 1800-2023
// 25.7.4): referenced via the interface instance, all task calls are
// disabled; referenced via a module instance, only the call to that module
// instance is.
//
// Derivation of the expected values (m1's body waits 10, m2's waits 20):
//  - A call sb.slow(1) starts at 0 and `disable sb.slow` runs at 5. Via the
//    interface it ends every body: m1.done and m2.done stay 0, and the call,
//    which returns when the last body ends, returns at 5 (r1 == 5).
//  - A call sb.slow(3) starts at 100 and `disable m1.a.slow` runs at 105.
//    Via the module instance it ends m1's body only: m1.done stays 0, m2's
//    body ends at 120 and adds 3, and the call returns at 120 (r2 == 120).
interface bus_if;
  extern forkjoin task slow(input int v);
  modport target(export slow);
endinterface

module mem #(parameter int D = 10) (bus_if.target a);
  int done = 0;
  task a.slow(input int v);
    #D;
    done += v;
  endtask
endmodule

//  - A call m1.a.slow(7) at 200 goes through the module instance (25.7.4): it
//    runs m1's body only. m1.done becomes 7, m2.done stays 3, and the call
//    returns at 210 (r3 == 210).
module top;
  bus_if sb ();
  mem #(10) m1 (sb.target);
  mem #(20) m2 (sb.target);
  int r1 = -1, r2 = -1, r3 = -1;

  initial begin
    sb.slow(1);
    r1 = $time;
  end
  initial begin
    #5 disable sb.slow;
  end

  initial begin
    #100;
    sb.slow(3);
    r2 = $time;
  end
  initial begin
    #105 disable m1.a.slow;
  end

  initial begin
    #200;
    m1.a.slow(7);
    r3 = $time;
  end

  initial begin
    #240;
    if (r1 != 5 || m1.done != 7 || m2.done != 3 || r2 != 120 || r3 != 210)
      $display("FAIL r1=%0d r2=%0d r3=%0d m1.done=%0d m2.done=%0d",
          r1, r2, r3, m1.done, m2.done);
    else
      $display("PASS");
  end
endmodule
