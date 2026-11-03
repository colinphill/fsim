// SPDX-License-Identifier: Apache-2.0
// A combinational loop that exceeds the delta-cycle limit at time 5: a
// run-time error that ends the simulation.
module top;
  wire a;
  reg go = 0;
  assign a = go ? ~a : 1'b0;
  initial #5 go = 1;
endmodule
