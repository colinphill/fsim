// SPDX-License-Identifier: Apache-2.0
module counter(input logic clock, input logic reset, output logic [1:0] value);
  always_ff @(posedge clock) begin
    if (reset)
      value <= 0;
    else
      value <= value + 1;
  end
endmodule
