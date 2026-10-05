// SPDX-License-Identifier: Apache-2.0
`timescale 1ns/1ps
module sampled_repeated_zero_time;
  logic clock = 1'b0;
  logic data = 1'b0;
  integer edge_index = 0;

  always @(posedge clock) begin
    edge_index = edge_index + 1;
    $display("ZERO edge=%0d past2=%b",
             edge_index,
             $past(data, 2, , @(posedge clock)));
  end

  initial begin
    #0 clock = 1'b1;
    #500ps data = 1'b1;
    #500ps clock = 1'b0;
    #0 clock = 1'b1;
    #0 clock = 1'b0;
    #0 clock = 1'b1;
    #1 clock = 1'b0;
    #0 clock = 1'b1;
    #1 $finish;
  end
endmodule
