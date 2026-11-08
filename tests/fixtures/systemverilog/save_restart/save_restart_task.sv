// SPDX-License-Identifier: Apache-2.0
// $save and $incsave (IEEE 1800-2023 D.9) from the design. Same counter as
// save_restart_cli.sv (its header derives save_restart_task.expected, the same
// ten lines). A second process saves in full at t=30 and incrementally at
// t=50; each file is taken at the end of the slot of its call:
//
//  - task_full.state is the state at the end of t=30, after the rising edge
//    at t=25 and the fall at t=30: a restart from it prints t=35 ... t=95
//    (save_restart_task.full.expected, counter 3 ... 9).
//  - task_inc.state is the state at the end of t=50 and names task_full.state
//    as its base: a restart from it prints t=55 ... t=95 (the same text as
//    save_restart_cli.restart.expected).
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
  initial begin
    #30 $save("task_full.state");
    #20 $incsave("task_inc.state");
  end
endmodule
