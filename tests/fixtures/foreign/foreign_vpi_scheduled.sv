// SPDX-License-Identifier: Apache-2.0
// Variables only a VPI library writes, with zero-delay scheduled writes made
// before time 0; the process reads them at time 1.
module top;
  typedef enum reg { FALSE = 1'b0, TRUE = 1'b1 } boolean;
  boolean transport;
  logic [3:0] inertial;
  logic [3:0] modified;
  initial #1 $display("#c %0t %b %0d %0d", $time, transport, inertial, modified);
endmodule
