// fsim: top=verilog_multidimensional_arrays std=2001 lang=verilog
// SPDX-License-Identifier: Apache-2.0
module verilog_multidimensional_arrays;
  reg [7:0] matrix [1:2][3:1];
  wire [7:0] nets [1:2][3:1];
  integer i, j;
  assign nets[2][2] = matrix[2][2];
  initial begin
    for (i = 1; i <= 2; i = i + 1)
      for (j = 1; j <= 3; j = j + 1)
        matrix[i][j] = i * 10 + j;
    #1;
    if (matrix[1][3] !== 13 || matrix[2][1] !== 21 || nets[2][2] !== 22)
      $display("FAIL: multidimensional Verilog array selection");
    else
      $display("PASS");
  end
endmodule
