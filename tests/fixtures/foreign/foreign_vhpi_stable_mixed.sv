// SPDX-License-Identifier: Apache-2.0
// The SystemVerilog half of the S'STABLE and S'QUIET mixed-language design:
// the ports of the VHDL leaf are driven by SystemVerilog work only, from
// static initializers (IEEE 1800-2023 6.8: initialization, no event).
module top;
  timeunit 1ns;
  logic p = 1'b0;
  logic [3:0] v = 4'h5;
  stable_leaf u(.p(p), .v(v));
  initial begin
    #5 p = 1'b1;
    #7 p = 1'b0;
    v = 4'h6;
    #1 $finish;
  end
endmodule
