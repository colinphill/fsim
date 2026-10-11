// fsim: top=sv_event_iff_and_local_parameters
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 9.4.2: an event control with an `iff` guard waits until
// the event occurs while the guard is true; 6.20.1: a parameter declared
// in a procedural block without a type is an integer; 21.2.1.7: `%s` of a
// packed value prints its characters.
module sv_event_iff_and_local_parameters;
  timeunit 1ns; timeprecision 1ns;
  logic clk = 0;
  logic en = 0;
  int count = 0;
  int guarded = 0;

  always #5 clk = ~clk;

  initial begin
    repeat (2) @(posedge clk iff en) guarded++;
  end

  always @(posedge clk) count++;

  initial begin
    #12 en = 1;
    #40;
    begin : local_block
      localparam step = 3;
      if (step + 1 != 4) $display("FAIL local parameter %0d", step);
    end
    if (guarded != 2) $display("FAIL guarded %0d", guarded);
    else if (count < 4) $display("FAIL count %0d", count);
    else $display("%s", 32'h50415353);
    $finish;
  end
endmodule
