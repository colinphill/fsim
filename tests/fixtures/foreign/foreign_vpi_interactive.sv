// SPDX-License-Identifier: Apache-2.0
// Interactive scope and interactive-mode callbacks (IEEE 1800-2023 38.4,
// 38.36.3): a batch $stop does not enter interactive mode.
module child;
endmodule

module top;
  reg v;
  child c ();
  initial begin
    #3;
    $stop;
  end
endmodule
