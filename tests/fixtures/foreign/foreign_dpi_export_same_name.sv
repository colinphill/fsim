// SPDX-License-Identifier: Apache-2.0
// A module and a generate block of it export functions of one SystemVerilog
// name (IEEE 1800-2023 35.7): the block is its own scope, so both export
// declarations are legal, and each is the function defined in its scope. A
// call from C runs in the scope the call chain is in (35.5.3): the module's
// function after svSetScope names the module instance, the block's after it
// names the block instance.
//
// Expected trace, derived from the LRM:
//   value 1 in top.u: module note; value 2 in top.u.blk: block note;
//   value 3 in top.u again: module note.
`timescale 1ns/1ps
module top;
  import "DPI-C" context task dpi_same_run();
  m u ();
  initial begin
    dpi_same_run();
    $display("#c run done");
  end
endmodule

module m;
  export "DPI-C" function dpi_same_note;
  function void dpi_same_note(input int value);
    $display("#c module note value=%0d", value);
  endfunction
  if (1) begin : blk
    export "DPI-C" function dpi_same_note;
    function void dpi_same_note(input int value);
      $display("#c block note value=%0d", value);
    endfunction
  end
endmodule
