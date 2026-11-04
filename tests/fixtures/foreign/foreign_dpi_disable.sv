// SPDX-License-Identifier: Apache-2.0
// DPI disable protocol (IEEE 1800-2023 35.9): disable fork ends a process
// that is inside an imported task, suspended in an exported task's delay.
`timescale 1ns/1ps
module top;
  import "DPI-C" context task dpi_disable_run(input int id);
  export "DPI-C" task dpi_disable_wait;
  task dpi_disable_wait(input int delay);
    #(delay);
    $display("#c export wait done at %0d", $time);
  endtask
  initial begin
    fork
      begin
        dpi_disable_run(0);
        $display("#c child continued");
      end
    join_none
    #5;
    $display("#c disabling at %0d", $time);
    disable fork;
    #15;
    dpi_disable_run(1);
    $display("#c end at %0d", $time);
  end
endmodule
