// SPDX-License-Identifier: Apache-2.0
module inactive_nested_delta;
  logic source_value;
  wire continuous_value;

  assign continuous_value = source_value;

  initial begin
    #1 source_value = 1'b0;
    #0 source_value = 1'b1;
  end

  initial begin
    #1;
    #0;
    if (source_value !== 1'b0 && source_value !== 1'b1)
      $fatal(1, "first Inactive observation was not binary");
    $display("INACTIVE race=accepted");

    #0;
    if (continuous_value !== source_value)
      $fatal(1, "continuous assignment did not settle before nested Inactive");
    $display("INACTIVE nested=settled");
    #1;
  end
endmodule
