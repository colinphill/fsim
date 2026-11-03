// SPDX-License-Identifier: Apache-2.0
// The SystemVerilog half of the mixed-language hierarchy design.
module mixed_sleaf(input logic a, output logic y);
  assign y = ~a;
  mixed_vleaf g(.a(a));
endmodule

module top;
  logic a = 1'b0;
  logic y;
  mixed_mid m(.a(a), .y(y));
  initial #3 $finish;
endmodule
