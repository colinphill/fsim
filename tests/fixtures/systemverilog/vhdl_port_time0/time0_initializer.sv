// SPDX-License-Identifier: Apache-2.0
// The VHDL in port shares the variable, whose declaration initializer sets
// its value before any process starts (IEEE 1800-2023 6.8): VHDL reads '0'.
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
