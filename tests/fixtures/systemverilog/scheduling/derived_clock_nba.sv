// SPDX-License-Identifier: Apache-2.0
module derived_clock_nba;
  logic data_value;
  logic source_clock;
  wire derived_clock;
  logic sampled_data;

  assign derived_clock = source_clock;

  always @(posedge derived_clock)
    sampled_data <= data_value;

  initial begin
    data_value = 1'b0;
    source_clock = 1'b0;
    sampled_data = 1'b0;
    #1;
    data_value = 1'b1;
    source_clock = 1'b1;
    data_value <= 1'b0;
    #1 $display("DERIVED sampled=%0b data=%0b", sampled_data, data_value);
  end
endmodule
