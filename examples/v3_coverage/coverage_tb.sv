// SPDX-License-Identifier: Apache-2.0
module coverage_tb;
  logic clock = 0;
  logic reset = 1;
  logic [1:0] value;
  counter dut(.*);

  covergroup values @(posedge clock);
    value_point: coverpoint value;
  endgroup
  values observed = new;

  always #1 clock = ~clock;
  initial begin
    repeat (2) @(posedge clock);
    reset = 0;
    repeat (6) @(posedge clock);
    $finish;
  end
endmodule
