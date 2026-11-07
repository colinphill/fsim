// SPDX-License-Identifier: Apache-2.0
// A variable never written reads x, which VHDL sees as 'X'.
module top;
  timeunit 1ns;
  logic p;
  stable_leaf u(.p(p));
  initial begin
    #5 p = 1'b1;
    #1 $finish;
  end
endmodule
