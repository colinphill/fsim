// SPDX-License-Identifier: Apache-2.0
// A SystemVerilog inout on a VHDL std_logic signal: its drivers are strong
// sources of the std_logic resolution, so a 0 overrides the VHDL pull-up.
module inout_svleaf(inout wire sda);
  timeunit 1ns;
  timeprecision 100ps;
  logic pull = 1'b0;
  assign sda = pull ? 1'b0 : 1'bz;
  initial begin
    #1.2 pull = 1'b1;
    #1 pull = 1'b0;
  end
  initial begin
    #0.5;
    for (int step = 0; step < 4; ++step) begin
      $display("sv %0d sda=%b", step, sda);
      #1;
    end
  end
endmodule
