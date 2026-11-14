// SPDX-License-Identifier: Apache-2.0
//
// A nonblocking assignment shall not write an element of a dynamically sized array (IEEE 1800-2023 10.4.2); 1800-2017 allows it.
module top;
  int dyn[];
  initial begin
    dyn = new[2];
    dyn[0] <= 1;
    #1 $display("nbarule element %0d", dyn[0]);
    $finish;
  end
endmodule
