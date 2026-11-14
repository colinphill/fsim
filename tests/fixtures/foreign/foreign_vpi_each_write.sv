// SPDX-License-Identifier: Apache-2.0
// Blocking writes of one activation, each a change of the variable: a
// cbValueChange registered on it runs once per write that changes the value
// (IEEE 1800-2023 38.36.1), and a write of a wide variable is one change
// however many words it stores.
module top;
  logic [3:0] x;
  logic [99:0] w;
  initial begin
    x = 4'd1;
    x = 4'd2;
    #1;
    x = 4'd3;
    x = 4'd3;
    x = 4'd4;
    w = {4'h1, 32'h0, 64'h1};
    w = {4'h2, 32'h0, 64'h2};
    x = 4'd5;
    #1 $finish;
  end
endmodule
