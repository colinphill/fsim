// SPDX-License-Identifier: Apache-2.0
module tb;
  logic       clk;
  logic       reset;
  logic [7:0] counter_q;
  logic [7:0] child_y;

  // The VHDL entity resolves uniquely by name in logical library work.
  counter u_counter (
    .clk(clk),
    .reset(reset),
    .q(counter_q)
  );

  sv_child u_child (
    .value(counter_q),
    .inverted(child_y)
  );

  initial begin
    clk = 1'b0;
    reset = 1'b1;
    #1 clk = 1'b1;
    #1 clk = 1'b0;
    #1 reset = 1'b0;
    #1 clk = 1'b1;
    #1 clk = 1'b0;
    #1 $finish;
  end
endmodule
