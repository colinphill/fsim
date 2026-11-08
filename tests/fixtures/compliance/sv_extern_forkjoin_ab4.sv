// fsim: top=top std=2017
// SPDX-License-Identifier: Apache-2.0
// `extern forkjoin task` (IEEE 1800-2023 25.7.3, 25.7.4): several module
// instances export one interface task, and a call behaves as
//   fork top.m1.a.count; top.m2.a.count; join
// so it runs every body, in parallel, and returns when the last one ends.
//
// Derivation of the expected values:
//  - sb.count() at time 0 runs m1's and m2's body once each: m1.hits == 1 and
//    m2.hits == 1.
//  - the interface's own process calls `count;` at time 5 (25.7.4 calls it by
//    its simple name from the interface): hits == 2 each by time 6.
//  - sb.add(5) at time 10 forks m1.add (waits #2, adds 5 * 1) and m2.add
//    (waits #1, adds 5 * 10); the call returns when the slower body ends, at
//    time 12: m1.acc == 5 and m2.acc == 50.
interface bus_if;
  extern forkjoin task count();
  extern forkjoin task add(input int v);
  modport target(export count, add);
  initial begin
    #5;
    count;
  end
endinterface

module mem #(parameter int K = 1, parameter int D = 1) (bus_if.target a);
  int hits = 0;
  int acc = 0;
  task a.count();
    hits++;
  endtask
  task a.add(input int v);
    #D;
    acc += v * K;
  endtask
endmodule

module top;
  bus_if sb ();
  mem #(1, 2) m1 (sb.target);
  mem #(10, 1) m2 (sb.target);
  bit ok = 1;

  initial begin
    sb.count();
    if (m1.hits != 1 || m2.hits != 1) begin
      $display("FAIL count %0d %0d", m1.hits, m2.hits);
      ok = 0;
    end
    #6;
    if (m1.hits != 2 || m2.hits != 2) begin
      $display("FAIL interface call %0d %0d", m1.hits, m2.hits);
      ok = 0;
    end
    #4;
    sb.add(5);
    if ($time != 12) begin
      $display("FAIL join time %0t", $time);
      ok = 0;
    end
    if (m1.acc != 5 || m2.acc != 50) begin
      $display("FAIL add %0d %0d", m1.acc, m2.acc);
      ok = 0;
    end
    if (ok) $display("PASS");
  end
endmodule
