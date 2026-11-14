// SPDX-License-Identifier: Apache-2.0
// Two register stages, SystemVerilog then VHDL, on one clock that a VPI
// plug-in drives through top-level ports nothing in the design drives
// (foreign_vpi_mixed_clock_plugin.c). The VHDL process woken by the clock
// edge reads the SystemVerilog stage's value from before the edge's
// nonblocking update (IEEE 1800-2023 4.4.2.2, 4.4.2.4; IEEE 1076-2019
// 14.7.5.3), so the second stage lags the first by one edge.
module top(input clk, input reset_n, input [7:0] din, input vin,
    output [7:0] q2);
  logic [7:0] q1;
  logic v1;
  always @(posedge clk or negedge reset_n)
    if (!reset_n) begin
      q1 <= 0;
      v1 <= 0;
    end else begin
      q1 <= din;
      v1 <= vin;
    end
  mixed_clock_stage u(.clk(clk), .reset_n(reset_n), .valid(v1), .d(q1),
    .q(q2));
endmodule
