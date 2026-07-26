// SPDX-License-Identifier: Apache-2.0
module tb;
  logic       clk;
  logic       reset;
  logic [7:0] counter_q;
  logic [7:0] child_y;

  // The source instance name is never used to guess across languages. The
  // schema-1 manifest binds this placeholder explicitly to the VHDL entity.
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
