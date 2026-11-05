// SPDX-License-Identifier: Apache-2.0
// DPI exports of the compilation unit (IEEE 1800-2023 35.5.2, 35.7): an
// export declared outside any module is callable from a context import in
// any scope, also after svSetScope names another instance, next to the
// exports that instance declares.
`timescale 1ns/1ps
export "DPI-C" function dpi_unit_note;
function void dpi_unit_note(input int value);
  $display("#c unit note value=%0d", value);
endfunction

module top;
  import "DPI-C" context task dpi_unit_run();
  a #(1) u1 ();
  initial begin
    dpi_unit_run();
    $display("#c run done");
  end
endmodule

module a #(parameter int ID = 0) ();
  export "DPI-C" function dpi_unit_local;
  function void dpi_unit_local(input int value);
    $display("#c local id=%0d value=%0d", ID, value);
  endfunction
endmodule
