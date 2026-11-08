// SPDX-License-Identifier: Apache-2.0
// $reset with a DPI import call chain in flight (IEEE 1800-2023 D.8, 35.9).
//
//  life 1 (count 0): the import runs and stores svPutUserData, then waits in
//          the export for 10 ns. At 5 ns the other branch calls $reset(1, 7).
//          D.8 a disables all activity, so the import is disabled: the export
//          returns 1 with the disabled state set and the import returns 1.
//  life 2 (count 1, value 7, time 0): the import finds no user data (the area
//          is gone after a reset), waits 3 ns in the export and completes.
`timescale 1ns/1ps
module top;
  import "DPI-C" context task dpi_reset_run(input int id);
  export "DPI-C" task dpi_reset_wait;
  task dpi_reset_wait(input int delay);
    #(delay);
    $display("#c export wait done at %0d", $time);
  endtask
  initial begin
    $display("#c life count=%0d value=%0d at %0d", $reset_count,
      $reset_value, $time);
    if ($reset_count == 0) begin
      fork
        dpi_reset_run(0);
        begin
          #5;
          $display("#c resetting at %0d", $time);
          $reset(1, 7);
        end
      join
    end else begin
      dpi_reset_run(1);
      $display("#c end at %0d", $time);
    end
  end
endmodule
