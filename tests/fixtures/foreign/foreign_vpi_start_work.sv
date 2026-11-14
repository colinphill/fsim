// SPDX-License-Identifier: Apache-2.0
// Static initialization, a package variable's initializer included, is
// complete before cbStartOfSimulation, which begins the time-zero cycle
// (IEEE 1800-2023 38.36.3, 6.21, 10.5): work the callback schedules (a
// cbAfterDelay, a put with a value-change callback registered) runs in that
// cycle and the following ones.
package pkg;
  int px = 3;
endpackage

module top;
  reg [7:0] q;
  initial begin
    #3 $finish;
  end
endmodule
