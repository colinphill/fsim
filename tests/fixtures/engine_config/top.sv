// SPDX-License-Identifier: Apache-2.0
module top;
  logic q = 1'b0;
  initial begin
    #1 q = 1'b1;
    #1 $finish;
  end
endmodule
