// SPDX-License-Identifier: Apache-2.0
// The SystemVerilog half of the S'STABLE and S'QUIET mixed-language design:
// the port of the VHDL leaf is driven by SystemVerilog work only.
module top;
  timeunit 1ns;
  logic p = 1'b0;
  stable_leaf u(.p(p));
  initial begin
    #5 p = 1'b1;
    #7 p = 1'b0;
    #1 $finish;
  end
endmodule
