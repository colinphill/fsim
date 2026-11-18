// SPDX-License-Identifier: Apache-2.0
// A cbValueChange registered after the writer has already run (time 2):
// each later blocking write that changes the variable is its own value
// change (IEEE 1800-2023 38.36.1), as it is for a watch registered at the
// start of simulation.
module top;
  logic [3:0] x;
  initial begin
    x = 4'd1;
    #3;
    x = 4'd2;
    x = 4'd3;
    x = 4'd3;
    x = 4'd4;
    #1 $finish;
  end
endmodule
