// SPDX-License-Identifier: Apache-2.0
//
// Sampled value functions without a clocking argument in a concurrent
// assertion take the assertion's clock (IEEE 1800-2023 16.9.3): their
// history follows the clock's ticks. The monitor's reads of them are kernel
// reads of the sampled histories, not host reads, also where the monitor
// evaluates between the clock's ticks (an asynchronous abort, 16.12.14).
//
// clk rises at 10, 20, 30 and 40; bus is 1 until 15, then 3; v is 0 until
// 25, then 1. Before the first tick a function compares with the default
// sampled value, the declaration's initial value (16.5.1): bus 1, v 0.
//   c1  cover property (@(posedge clk) $stable(bus)): true at 10, 30 and
//       40, false at 20 (1 to 3): n1 counts 3.
//   c2  cover property (@(posedge clk) $rose(v)): true at 30 only.
//   c3  cover property (@(posedge clk) $changed(bus) or $past(v)): true at
//       20 ($changed) and at 40 ($past(v) is v sampled at 30, 1): n3
//       counts 2.
//   p1  assert property (@(posedge clk) accept_on(q) $stable(bus) ##1 1),
//       q never 1: the attempt from 10 succeeds at 20, the one from 20 fails
//       there ($stable false), the one from 30 succeeds at 40; the one from
//       40 is still in progress when the run ends at 50 (no outcome).
module top;
  logic clk = 0;
  logic [7:0] bus = 1;
  logic v = 0, q = 0;
  int n1 = 0, n2 = 0, n3 = 0;
  initial begin
    #10 clk = 1;
    #5 clk = 0;
    bus = 3;     // 15
    #5 clk = 1;  // 20
    #5 clk = 0;
    v = 1;       // 25
    #5 clk = 1;  // 30
    #5 clk = 0;
    #5 clk = 1;  // 40
    #5 clk = 0;
    #5 $display("inl n1=%0d n2=%0d n3=%0d", n1, n2, n3);
    $finish;
  end
  c1: cover property (@(posedge clk) $stable(bus)) n1++;
  c2: cover property (@(posedge clk) $rose(v)) n2++;
  c3: cover property (@(posedge clk) $changed(bus) or $past(v)) n3++;
  p1: assert property (@(posedge clk) accept_on(q) $stable(bus) ##1 1)
    $display("inl p1 pass t=%0t", $time);
  else $display("inl p1 fail t=%0t", $time);
endmodule
