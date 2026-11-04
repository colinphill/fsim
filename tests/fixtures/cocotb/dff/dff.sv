// SPDX-License-Identifier: Apache-2.0
// A D flip-flop with a synchronous reset; no `timescale, so cocotb's default
// time unit and precision apply (compile --timescale).
module dff (
  input  logic       clk,
  input  logic       rst,
  input  logic [7:0] d,
  output logic [7:0] q
);
  always_ff @(posedge clk)
    q <= rst ? 8'd0 : d;
endmodule
