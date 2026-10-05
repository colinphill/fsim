// SPDX-License-Identifier: Apache-2.0
module continuous_active;
  logic source_value;
  wire continuous_value;

  assign continuous_value = source_value;

  initial begin
    source_value = 1'b0;
    #1 source_value = 1'b1;
    #0 $display("DIRECT value=%0b", continuous_value);
    #1;
  end
endmodule
