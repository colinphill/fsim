// SPDX-License-Identifier: Apache-2.0
module fsim_sensitivity_ranges_wide;
  reg [128:0] input_zero = 0;
  reg [128:0] input_one = 0;
  reg index = 0;
  wire [128:0] elements [1:0];
  assign elements[0] = input_zero;
  assign elements[1] = input_one;
  reg [64:0] comb_value, wildcard_value;
  reg [128:0] dynamic_value;
  always_comb begin
    comb_value = elements[0][64:0];
    $display("EVENT comb");
  end
  always @* begin
    wildcard_value = elements[0][64:0];
    $display("EVENT wildcard");
  end
  always_comb begin
    dynamic_value = elements[index];
    $display("EVENT dynamic");
  end
  always @(elements[0]) $display("EVENT explicit_element");
  always @(elements[0][64:0]) $display("EVENT explicit_slice");
  initial begin
    #1;
    $display("BEGIN unrelated_element");
    input_one = 129'h1;
    #1;
    $display("END unrelated_element");
    $display("BEGIN unrelated_bits");
    input_zero = 129'h1_0000000000000000_0000000000000000;
    #1;
    $display("END unrelated_bits");
    $display("BEGIN selected_bits");
    input_zero = 129'h1_0000000000000000_0000000000000001;
    #1;
    $display("END selected_bits");
    $display("BEGIN dynamic_index");
    index = 1;
    #1;
    $display("END dynamic_index");
    $finish;
  end
endmodule
