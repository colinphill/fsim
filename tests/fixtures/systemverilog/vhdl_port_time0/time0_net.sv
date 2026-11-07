// SPDX-License-Identifier: Apache-2.0
// The VHDL in port reads the net through the logic mapping; the order of
// the time-0 assignment and the VHDL initialization is unspecified.
module top;
  timeunit 1ns;
  wire p;
  assign p = 1'b0;
  stable_leaf u(.p(p));
  initial #13 $finish;
endmodule
