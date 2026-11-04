// SPDX-License-Identifier: Apache-2.0
// A SystemVerilog top over a VHDL child: cocotb reaches both languages.
module mixed_top (
  input  logic       clk,
  input  logic [7:0] a,
  input  logic [7:0] b,
  output logic [7:0] sum,
  output logic [7:0] staged
);
  logic [7:0] registered;
  mixed_adder u_adder (.a(a), .b(b), .y(sum));
  always_ff @(posedge clk) registered <= sum;
  // The VHDL flop samples `registered` from before each edge, as a second
  // SystemVerilog flop would.
  mixed_flop u_flop (.clk(clk), .d(registered), .q(staged));
endmodule
