// SPDX-License-Identifier: Apache-2.0
module monitor_once_per_slot;
  logic value;
  logic trigger;

  initial begin
    value = 1'b0;
    trigger = 1'b0;
    $monitor("MON value=%0b", value);
    #1;
    value = 1'b1;
    value = 1'b0;
    trigger <= 1'b1;
    #1;
  end

  always @(posedge trigger)
    value <= 1'b1;
endmodule
