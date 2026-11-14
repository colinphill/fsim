// SPDX-License-Identifier: Apache-2.0
// The SystemVerilog leaf of the mixed initialization design: a continuous
// assignment of its input and an initial procedure that drives an output at
// time zero (IEEE 1800-2023 4.4, 4.5).
module leaf(input logic a, output logic y, output logic [3:0] q);
  assign y = a;
  initial q = 4'b0110;
endmodule
