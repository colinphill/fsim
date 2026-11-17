// SPDX-License-Identifier: Apache-2.0
//
// $monitor and $strobe of variables and nets the v4 kernel holds (IEEE
// 1800-2023 21.2.2, 21.2.3): the monitor prints at the end of each time step
// in which one of its arguments changed, a change undone within the step
// included, and not while $monitoroff is in effect; $strobe prints the
// values at the end of its time step. A $monitor replacing another prints
// once in its own time step.
module kernel_monitor_cell(input logic [7:0] a, output logic [7:0] b);
  assign b = a + 1;
endmodule

module kernel_monitor;
  logic [7:0] a = 0, doubled;
  wire [7:0] y;
  real r = 0.5;
  int k;
  kernel_monitor_cell c (.a(a), .b(y));
  always @(y) doubled = y * 2;
  initial $monitor("%0t a=%0d y=%0d doubled=%0d r=%f", $time, a, y, doubled, r);
  initial begin
    for (k = 0; k < 6; k++) begin
      #1 a = a + 3;
      r = r * 2;
      $strobe("strobe %0t y=%0d doubled=%0d", $time, y, doubled);
      if (k == 3) begin
        a = a + 1;
        a = a - 1;
      end
    end
    #1 $monitoroff;
    a = 100;
    #1 $monitoron;
    // A new monitor replaces the old one and prints once in the step it is
    // installed in, after writes of that step.
    #1 a = 7;
    $monitor("again %0t y=%0d", $time, y);
    #1 a = 9;
    #1 $finish;
  end
endmodule
