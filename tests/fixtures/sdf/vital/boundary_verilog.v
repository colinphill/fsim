// SPDX-License-Identifier: Apache-2.0
// FSIM-VITAL-BOUNDARY: verilog
module fsim_vital_verilog_boundary(input wire a, output wire z);
  assign #1 z = a;
endmodule
