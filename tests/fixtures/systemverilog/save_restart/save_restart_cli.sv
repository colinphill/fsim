// SPDX-License-Identifier: Apache-2.0
// `--save-at` and `--restart` (IEEE 1800-2023 D.9): a counter clocked by a
// bounded clock. Expected output, derived by hand:
//
// clk starts at 0 and toggles every 5 time units, twenty times, so it rises
// at 5, 15, 25, ..., 95 and falls at 10, 20, ..., 100. Each rising edge prints
// the time and the counter before the edge's own increment (the increment is
// a nonblocking assignment, so the display reads the old value): counter is 0
// at t=5, 1 at t=15, ..., 9 at t=95. save_restart_cli.expected is those ten
// lines.
//
// With `--save-at 50` the state is saved at the end of the slot at t=50 (clk
// falls there, so nothing prints). The saving run goes on and prints the same
// ten lines. A restart from that file continues at t=55: the lines for
// t=55 ... t=95, counter 5 ... 9 (save_restart_cli.restart.expected).
`timescale 1ns/1ns
module top;
  reg clk;
  reg [7:0] counter;
  initial begin
    clk = 0;
    counter = 0;
    repeat (20) #5 clk = ~clk;
  end
  always @(posedge clk) begin
    $display("t=%0d counter=%0d", $time, counter);
    counter <= counter + 1;
  end
endmodule
