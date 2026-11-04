// SPDX-License-Identifier: Apache-2.0
// DPI export dispatch by scope (IEEE 1800-2023 35.5.3, 35.7): a context
// import of a module that declares no export sets the scope of another
// instance and calls the exports declared there; a package is no scope
// svGetScopeFromName names (H.9.3); a scope that does not declare the
// export is an error.
`timescale 1ns/1ps
package pk;
  int pk_total;
  export "DPI-C" function dpi_dispatch_pkg_note;
  function void dpi_dispatch_pkg_note(input int value);
    pk_total += value;
    $display("#c pkg note value=%0d total=%0d", value, pk_total);
  endfunction
endpackage

module top;
  import "DPI-C" context task dpi_dispatch_run();
  import "DPI-C" context task dpi_dispatch_bad();
  c #(1) u1 ();
  c #(2) u2 ();
  initial begin
    #5;
    dpi_dispatch_run();
    $display("#c run done time=%0d pk_total=%0d", $time, pk::pk_total);
    dpi_dispatch_bad();
    $display("#c unreachable");
  end
endmodule

module c #(parameter int ID = 0) ();
  export "DPI-C" function dpi_dispatch_note;
  export "DPI-C" task dpi_dispatch_wait;
  int total;
  function void dpi_dispatch_note(input int value);
    total += value;
    $display("#c note id=%0d value=%0d total=%0d", ID, value, total);
  endfunction
  task dpi_dispatch_wait(input int delay);
    #(delay);
    $display("#c wait id=%0d time=%0d", ID, $time);
  endtask
endmodule
