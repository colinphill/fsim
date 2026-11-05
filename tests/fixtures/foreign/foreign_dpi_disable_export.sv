// SPDX-License-Identifier: Apache-2.0
// DPI disable protocol (IEEE 1800-2023 35.9): an exported task that is itself
// the target of a disable returns 0, and its imported parent is not in the
// disabled state.
`timescale 1ns/1ps
module top;
  import "DPI-C" context task dpi_target_run();
  export "DPI-C" task dpi_target_wait;
  task dpi_target_wait(input int delay);
    #(delay);
    $display("#c export wait done at %0d", $time);
  endtask
  initial begin
    dpi_target_run();
    $display("#c run returned at %0d", $time);
  end
  initial begin
    #5;
    $display("#c disabling the export at %0d", $time);
    disable dpi_target_wait;
  end
endmodule
