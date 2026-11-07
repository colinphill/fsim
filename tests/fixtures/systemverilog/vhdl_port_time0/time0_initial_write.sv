// SPDX-License-Identifier: Apache-2.0
// A time-0 procedural write changes the variable from x to 0; the order of
// the write and the VHDL initialization is unspecified.
module top;
  timeunit 1ns;
  logic p;
  stable_leaf u(.p(p));
  initial begin
    p = 1'b0;
    #5 p = 1'b1;
    #7 p = 1'b0;
    #1 $finish;
  end
endmodule
