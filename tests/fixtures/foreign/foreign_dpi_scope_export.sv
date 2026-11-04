// SPDX-License-Identifier: Apache-2.0
// DPI export dispatch (IEEE 1800-2023 35.5.3, 35.7, 35.8): a context import
// calls the exports of its own instance, also after svSetScope names that
// instance, and a noncontext import cannot call an export (a fatal error).
`timescale 1ns/1ps
module top;
  import "DPI-C" function void dpi_scope_bad();
  a #(0) u0 ();
  a #(1) u1 ();
  initial begin
    #20;
    dpi_scope_bad();
    $display("#c unreachable");
  end
endmodule

module a #(parameter int ID = 0) ();
  import "DPI-C" context task dpi_scope_run(input int id);
  export "DPI-C" function dpi_scope_note;
  export "DPI-C" task dpi_scope_wait;
  int total;
  function void dpi_scope_note(input int value);
    total += value;
    $display("#c note id=%0d value=%0d total=%0d", ID, value, total);
  endfunction
  task dpi_scope_wait(input int delay);
    #(delay);
    $display("#c wait id=%0d time=%0d", ID, $time);
  endtask
  initial begin
    #(ID * 10 + 1);
    dpi_scope_run(ID);
  end
endmodule
