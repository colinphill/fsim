// SPDX-License-Identifier: Apache-2.0
// DPI disable protocol (IEEE 1800-2023 35.9): disable fork ends a process
// that is inside an imported task, suspended in an exported task's delay;
// so does a disable of a named block that called a task that called the
// import (9.6.2: the block's activity includes the tasks it enabled).
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
  task wrap();
    dpi_disable_run(2);
    $display("#c wrap continued");
  endtask
  initial begin
    #30;
    begin : bw
      wrap();
      $display("#c bw continued");
    end
    $display("#c after bw at %0d", $time);
  end
  initial begin
    #32;
    $display("#c disabling bw at %0d", $time);
    disable bw;
  end
endmodule
