// SPDX-License-Identifier: Apache-2.0
//
// SystemVerilog-2023 allows a nonblocking assignment to an element of a fixed
// string array only (IEEE 1800-2023 10.4.2): the update is written in the NBA
// region, and writes to two elements of the array in one time step both land.
module top;
  string fx[3];
  initial begin
    fx[0] = "a"; fx[1] = "b"; fx[2] = "c";
    fx[0] <= "A";
    fx[2] <= "C";
    $display("sns fixed same-step %s,%s,%s", fx[0], fx[1], fx[2]);
    #1;
    $display("sns fixed next-step %s,%s,%s", fx[0], fx[1], fx[2]);
  end
endmodule
