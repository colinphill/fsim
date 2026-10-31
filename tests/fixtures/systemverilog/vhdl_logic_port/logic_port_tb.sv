// SPDX-License-Identifier: Apache-2.0
module logic_port_tb;
  logic clk = 1'bx;
  logic [3:0] v = 4'bzz10;
  logic_port_dut dut(.clk(clk), .v(v));
  initial begin
    #1 clk = 1'b0; v = 4'b1111;
    #1 clk = 1'b1;
    #1 clk = 1'bz; v = 4'bx001;
    #1 $finish;
  end
endmodule
