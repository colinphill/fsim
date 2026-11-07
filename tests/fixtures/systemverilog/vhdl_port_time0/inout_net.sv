// SPDX-License-Identifier: Apache-2.0
// SystemVerilog nets on VHDL inout and out ports. A VHDL driver starts at its
// port's default ('U' reads as x, IEEE 1076-2019 14.7.2), also against a
// SystemVerilog z; 'H', 'L' and 'W' are weak on the net, so the VHDL pull-up
// on sda yields to a SystemVerilog 0; both sides read the resolved net
// through the logic mapping (docs/cross-language-semantics.md).
module top;
  timeunit 1ns;
  timeprecision 100ps;
  wire io, sda, o, d;
  logic pull = 1'b0;
  assign io = 1'bz;
  assign sda = pull ? 1'b0 : 1'bz;
  inout_leaf u(.io(io), .sda(sda), .o(o), .d(d));
  initial begin
    #1.2 pull = 1'b1;
    #1 pull = 1'b0;
  end
  initial begin
    #0.5;
    for (int step = 0; step < 8; ++step) begin
      $display("sv %0d io=%b sda=%b o=%b d=%b", step, io, sda, o, d);
      #1;
    end
    $finish;
  end
endmodule
