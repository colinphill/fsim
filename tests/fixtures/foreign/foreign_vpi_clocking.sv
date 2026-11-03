// SPDX-License-Identifier: Apache-2.0
// Clocking blocks (IEEE 1800-2023 37.48, 14.3, 14.12, 14.14) and user-defined
// nettypes (37.23) as VPI objects.
module top;
  logic clk = 0;
  logic [3:0] data, res, mon;
  logic ack, rdy, ack2;
  nettype real real_t;
  nettype real_t alias_t;
  function automatic real pick(input real drivers[]);
    pick = drivers[0];
  endfunction
  nettype real pick_t with pick;
  default clocking cb @(posedge clk);
    default input #1 output #2;
    input data;
    output res;
    input #3 ack;
    inout rdy;
  endclocking
  clocking cb2 @(clk);
    input #1step ack2;
    output #4 mon;
  endclocking
  global clocking gck @(posedge clk);
  endclocking
  initial #3 $finish;
endmodule
