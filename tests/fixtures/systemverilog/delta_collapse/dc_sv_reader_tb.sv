// SPDX-License-Identifier: Apache-2.0
// Collapsed VHDL cells whose product an SV flop samples through a port.
module dc_sv_reader_tb;
  logic clk = 1'b0;
  logic [3:0] a = 4'd2;
  wire [3:0] rotated = {a[2:0], a[3]};
  wire [3:0] p1, p2;
  logic [7:0] r;
  dc_static_cell u1(.a(a), .b(rotated), .p(p1));
  dc_static_cell u2(.a(p1), .b(a), .p(p2));
  always #5 clk = ~clk;
  always @(posedge clk) r <= {p1, p2};
  initial repeat (16) #7 a = a + 4'd5;
  initial #130 $finish;
  always @(negedge clk) $display("%0t r=%h", $time, r);
endmodule
