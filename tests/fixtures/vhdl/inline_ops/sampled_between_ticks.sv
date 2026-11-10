// SPDX-License-Identifier: Apache-2.0
//
// Value change functions with an explicit clocking event, called in time
// steps that are not ticks of that clock: in procedural code and in
// assertions of another clock. IEEE 1800-2023 16.9.3: the function compares
// "the sampled value of the expression with the sampled value of the
// expression from the most recent strictly prior time step in which the
// clocking event occurred"; at or before the time step of the first tick it
// compares with the default sampled value (the value at the start, 16.5.1).
// $past (16.9.3) gives the sample of the most recent strictly prior tick.
//
// The clock clk has its posedges at 15, 25, 35, 45 and 55; clk2 at 23, 33
// and 43. r is 0, then 1 from 22, 0 from 32 and 1 from 47. bus (8 bits) is
// 1, then 3 from 8 and 7 from 28 (the LSB never changes). w (100 bits) is 0,
// then bit 90 and bit 0 set from 30. The samples at the posedges of clk:
//
//   tick   15  25  35  45  55
//   r       0   1   0   0   1
//   bus     3   3   7   7   7
//   w[0]    0   0   1   1   1  (w == 0 at 15 and 25)
//
// Procedural reads (all with @(posedge clk)): rose/fell/stable/changed of r,
// changed of bus, changed and rose of w, past of r. The current value is the
// sampled value of the time step (a change in this time step is not seen:
// t=22), the previous one the most recent tick strictly before it (t=25 is a
// tick: still compared with 15, whether the read runs before or after the
// posedge in that time step):
//
//   t     r: now vs prior tick       rose fell stable changed  bus  wchg wrose past
//   10    0 vs default 0             0    0    1      0        1    0    0     0
//         (bus 3 vs default 1, w 0 vs default 0, $past the default)
//   22    0 vs 15: 0 (r rose in this time step; its sampled value is 0)
//                                    0    0    1      0        0    0    0     0
//   23    1 vs 15: 0                 1    0    0      1        0    0    0     0
//   25    1 vs 15: 0                 1    0    0      1        0    0    0     0
//         (25 is a tick, but not strictly prior; bus 3 vs 3)
//   26    1 vs 25: 1                 0    0    1      0        0    0    0     1
//   29    1 vs 25: 1 (bus 7 vs 3)    0    0    1      0        1    0    0     1
//   31    1 vs 25: 1 (w changed at 30, w[0] rose)
//                                    0    0    1      0        1    1    1     1
//   33    0 vs 25: 1                 0    1    0      1        1    1    1     1
//   36    0 vs 35: 0 (bus 7 vs 7, w as at 35)
//                                    0    0    1      0        0    0    0     0
//   48    1 vs 45: 0                 1    0    0      1        0    0    0     0
//   55    1 vs 45: 0 (a tick)        1    0    0      1        0    0    0     0
//   56    1 vs 55: 1                 0    0    1      0        0    0    0     1
//
// Assertions clocked by clk2 (23, 33, 43) with the functions on clk; each
// cover adds the time of its successes:
//
//   $rose(r)      23: 1 vs 15: 0 yes; 33: 0; 43: 0 vs 35: 0      sum 23
//   $fell(r)      33: 0 vs 25: 1 yes                               sum 33
//   $changed(bus) 23: 3 vs 15: 3; 33: 7 vs 25: 3 yes; 43: 7 vs 35  sum 33
//   $stable(w)    23 yes; 33: changed at 30 vs 25; 43 yes          sum 66
//   $rose(w)      33: w[0] 1 vs 25: 0 yes                          sum 33
module top;
  logic clk = 0, clk2 = 0;
  logic r = 0;
  logic [7:0] bus = 1;
  logic [99:0] w = '0;
  int rose_sum = 0, fell_sum = 0, bus_sum = 0, wstable_sum = 0, wrose_sum = 0;
  initial begin
    #15;
    repeat (5) begin
      clk = 1; #2 clk = 0; #8;
    end
  end
  initial begin
    #23;
    repeat (3) begin
      clk2 = 1; #2 clk2 = 0; #8;
    end
  end
  initial begin
    #8 bus = 3;
    #14 r = 1;          // 22
    #6 bus = 7;         // 28
    #2 w[90] = 1; w[0] = 1; // 30
    #2 r = 0;           // 32
    #15 r = 1;          // 47
  end
  task automatic probe;
    $display("inl t=%0t rose=%0b fell=%0b stable=%0b changed=%0b bus=%0b wchg=%0b wrose=%0b past=%0b",
      $time, $rose(r, @(posedge clk)), $fell(r, @(posedge clk)),
      $stable(r, @(posedge clk)), $changed(r, @(posedge clk)),
      $changed(bus, @(posedge clk)), $changed(w, @(posedge clk)),
      $rose(w, @(posedge clk)), $past(r, 1, , @(posedge clk)));
  endtask
  initial begin
    #10 probe();        // 10
    #12 #0 probe();     // 22, after r rose
    #1 probe();         // 23
    #2 probe();         // 25
    #1 probe();         // 26
    #3 probe();         // 29
    #2 probe();         // 31
    #2 probe();         // 33
    #3 probe();         // 36
    #12 probe();        // 48
    #7 probe();         // 55
    #1 probe();         // 56
    #4;
    $display("inl clk2 rose=%0d fell=%0d bus=%0d wstable=%0d wrose=%0d",
      rose_sum, fell_sum, bus_sum, wstable_sum, wrose_sum);
    $finish;
  end
  c_rose: cover property (@(posedge clk2) $rose(r, @(posedge clk))) rose_sum += $time;
  c_fell: cover property (@(posedge clk2) $fell(r, @(posedge clk))) fell_sum += $time;
  c_bus: cover property (@(posedge clk2) $changed(bus, @(posedge clk))) bus_sum += $time;
  c_wstable: cover property (@(posedge clk2) $stable(w, @(posedge clk))) wstable_sum += $time;
  c_wrose: cover property (@(posedge clk2) $rose(w, @(posedge clk))) wrose_sum += $time;
endmodule
