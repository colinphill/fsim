// SPDX-License-Identifier: Apache-2.0
module scheduling_port_leaf(input wire a, output wire y);
  assign y = a;
endmodule

module implicit_port_active;
  logic [1:0] stimulus;
  wire [1:0] response;

  scheduling_port_leaf leaf(.a(stimulus[1]), .y(response[0]));

  initial begin
    stimulus = 2'b00;
    #1 stimulus[1] = 1'b1;
    #0 $display("PORT input=%0b output=%0b", stimulus[1], response[0]);
    #1;
  end
endmodule
