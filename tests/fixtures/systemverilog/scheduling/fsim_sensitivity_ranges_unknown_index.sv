// SPDX-License-Identifier: Apache-2.0
module fsim_sensitivity_ranges_unknown_index;
  reg [7:0] input_zero = 8'h12;
  reg [7:0] input_one = 8'h34;
  reg [1:0] index = 2'b00;
  wire [7:0] elements [1:0];
  assign elements[0] = input_zero;
  assign elements[1] = input_one;
  reg [7:0] dynamic_value;

  always_comb begin
    dynamic_value = elements[index];
    $display("EVENT dynamic");
  end

  always @(elements[index]) $display("EVENT explicit_dynamic");

  initial begin
    #1;
    $display("BEGIN known_to_x");
    index = 2'bx0;
    #1;
    $display("END known_to_x");

    $display("BEGIN x_invalid_writes");
    input_zero = 8'h56;
    #1;
    input_one = 8'h78;
    #1;
    $display("END x_invalid_writes");

    $display("BEGIN x_to_z");
    index = 2'bz0;
    #1;
    $display("END x_to_z");

    $display("BEGIN z_invalid_writes");
    input_zero = 8'h9a;
    #1;
    input_one = 8'hbc;
    #1;
    $display("END z_invalid_writes");

    $display("BEGIN z_to_known");
    index = 2'b01;
    #1;
    $display("END z_to_known");
    $finish;
  end
endmodule
