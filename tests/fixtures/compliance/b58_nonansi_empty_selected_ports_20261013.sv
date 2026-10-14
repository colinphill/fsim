// fsim: top=b58_nonansi_empty_selected_ports_20261013 std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 23.2.2.1: non-ANSI selected port references and ordered
// positional connections preserve empty header slots; 23.3.2.4: wildcard
// connection associates named ports without inventing a name for an empty slot.
module b58_nonansi_empty_selected_ports_20261013;
  reg [7:0] data;
  reg dummy_1;
  reg dummy_2;
  wire [7:0] open_result;
  wire [7:0] expression_result;
  wire [7:0] result;

  b58_nonansi_selected_child u_open(
      dummy_1, , data, dummy_2, open_result, );
  b58_nonansi_selected_child u_expression(
      dummy_1, dummy_2, data, dummy_2, expression_result, dummy_1);
  b58_nonansi_selected_child u_wildcard(.*);

  initial begin
    data = 8'ha5;
    dummy_1 = 1'b0;
    dummy_2 = 1'b1;
    #1;
    if (open_result !== 8'ha5
        || expression_result !== 8'ha5
        || result !== 8'ha5) begin
      $fatal(1, "non-ANSI selected port positions changed");
    end
    $display("PASS");
  end
endmodule

module b58_nonansi_selected_child(dummy_1, , data[7:0], dummy_2,
                                  result[7:0],);
  input dummy_1;
  input [7:0] data;
  input dummy_2;
  output [7:0] result;
  assign result = data;
endmodule
