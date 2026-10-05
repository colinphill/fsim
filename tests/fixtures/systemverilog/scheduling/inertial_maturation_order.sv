// SPDX-License-Identifier: Apache-2.0
module inertial_maturation_order;
  logic source;
  wire delayed;

  assign #2 delayed = source;

  initial begin
    source = 1'b0;
    #1 source = 1'b1;
    #1 source = 1'b0;
    #2;
    #0 $display("INERTIAL canceled=%0b", delayed);
    #1 source = 1'b1;
    #2;
    #0 $display("INERTIAL matured=%0b", delayed);
    #1 $finish;
  end
endmodule
