// SPDX-License-Identifier: Apache-2.0
// Clocking blocks (IEEE 1800-2023 37.48, 14.3, 14.12, 14.14) and user-defined
// nettypes (37.23), nets of a nettype and interconnect nets and arrays
// (37.16, 37.24) as VPI objects.
typedef struct packed { logic [1:0] hi; logic [1:0] lo; } quad_t;
typedef struct packed { logic a; logic b; } duo_t;
// Typed ports an interconnect net connects (6.6.8, 37.24 detail 1).
module quad_sink (input quad_t p);
endmodule
module duo_sink (input duo_t [1:0] p);
endmodule
module vec_sink (input logic [3:0] p);
endmodule

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
  nettype logic [3:0] vec_t;
  // A net of a nettype (6.6.7) and generic interconnect nets (6.6.8).
  vec_t nv;
  interconnect ic;
  interconnect [3:0] icv;
  interconnect [3:0] ica [1:0];
  // Interconnect nets that take their type from the typed port they connect.
  interconnect [3:0] icq;
  interconnect [3:0] icd;
  quad_sink qs (.p(icq));
  duo_sink ds (.p(icd));
  vec_sink vs (.p(icv));
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
