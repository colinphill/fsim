// SPDX-License-Identifier: Apache-2.0
module strobe_end_of_slot_verilog;
  reg value;
  reg trigger;

  initial begin
    value = 1'b0;
    trigger = 1'b0;
    #1;
    $strobe("STROBE value=%0b", value);
    trigger <= 1'b1;
    #1;
  end

  always @(posedge trigger)
    value <= 1'b1;
endmodule
