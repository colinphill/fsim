// SPDX-License-Identifier: Apache-2.0
// DPI exports declared in generate blocks (IEEE 1800-2023 35.5.3, 35.7): an
// export is in the scope of the function it exports, so a context import
// calls it after svSetScope names that block instance, also for one
// iteration of a loop generate; a block that is not generated has no export.
`timescale 1ns/1ps
module top;
  import "DPI-C" context task dpi_gen_run();
  host h ();
  initial begin
    dpi_gen_run();
    $display("#c run done");
  end
endmodule

module host;
  if (1) begin : blk
    int total;
    export "DPI-C" function dpi_gen_note;
    function void dpi_gen_note(input int value);
      total += value;
      $display("#c blk note value=%0d total=%0d", value, total);
    endfunction
  end
  if (0) begin : off
    export "DPI-C" function dpi_gen_off;
    function void dpi_gen_off(input int value);
      $display("#c unreachable %0d", value);
    endfunction
  end
  for (genvar i = 0; i < 2; i++) begin : lp
    export "DPI-C" task dpi_gen_wait;
    task dpi_gen_wait(input int delay);
      #(delay);
      $display("#c lp[%0d] wait time=%0d", i, $time);
    endtask
  end
endmodule
