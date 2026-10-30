// SPDX-License-Identifier: Apache-2.0
`timescale 1ns/1ps
module cd_edge_tb;
  logic [3:0] count;
  cd_edge dut(.count(count));
  initial #20 $display("count=%0d", count);
endmodule
