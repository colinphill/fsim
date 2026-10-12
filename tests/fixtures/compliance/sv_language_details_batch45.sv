// fsim: top=sv_language_details_batch45
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 7.10: queue `$` indexing and out-of-range queue methods;
// 23.3.3: an output port connected to a concatenation; 21.2.1.3: partially
// unknown hexadecimal digits; 25.5: modport expressions and a modport
// selected by the actual; 12.7: several for-loop initializers and general
// updates, foreach with a skipped dimension; 6.8: generated variable
// initializers; 11.11: min:typ:max.
`timescale 1ns / 1ns
interface batch45_if;
  logic [3:0] raw_in;
  logic [3:0] raw_out;
  modport sink(input .data(raw_in), output .result(raw_out));
endinterface

module batch45_sink(batch45_if.sink port);
  assign port.result = port.data + 4'd1;
endmodule

module batch45_split(input [3:0] value, output [3:0] doubled);
  assign doubled = value << 1;
endmodule

module sv_language_details_batch45;
  localparam int TYPICAL = (1:2:3);
  int queue_values[$];
  logic [3:0] grid[2][3][4];
  int visits;
  logic [1:0] high, low;
  wire [7:0] partial;
  string text;
  batch45_if bus();
  batch45_sink sink(.port(bus.sink));
  batch45_split split(.value(4'd3), .doubled({high, low}));
  assign partial[3:0] = 4'h5;

  for (genvar g = 0; g < 2; g++) begin : gen
    int seed = g + 10;
  end

  initial begin
    int popped, sum_i, sum_j;
    bus.raw_in = 4'd6;
    queue_values = '{1, 2, 3};
    queue_values[$] = 7;
    queue_values.delete(9);
    queue_values.insert(-1, 5);
    popped = queue_values[$ - 1];
    visits = 0;
    foreach (grid[i, , k]) visits++;
    sum_i = 0;
    sum_j = 0;
    for (int i = 0, j = 10; i < 3; i++, j -= 2) begin
      sum_i += i;
      sum_j += j;
    end
    #1;
    text = $sformatf("%h", partial);
    if (queue_values.size() != 3 || queue_values[2] != 7 || popped != 2)
      $display("FAIL queue %0d %0d", queue_values.size(), popped);
    else if ({high, low} != 4'd6) $display("FAIL concatenation port");
    else if (text != "z5") $display("FAIL hex %s", text);
    else if (bus.raw_out != 4'd7) $display("FAIL modport expression");
    else if (visits != 8) $display("FAIL foreach %0d", visits);
    else if (sum_i != 3 || sum_j != 24) $display("FAIL for %0d %0d", sum_i, sum_j);
    else if (gen[1].seed != 11) $display("FAIL generated initializer");
    else if (TYPICAL != 2) $display("FAIL min:typ:max");
    else $display("PASS");
  end
endmodule
