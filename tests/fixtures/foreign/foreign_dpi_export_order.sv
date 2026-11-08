// SPDX-License-Identifier: Apache-2.0
// DPI exports of instances elaborated after the calling instance (IEEE
// 1800-2023 35.5.3, 35.7): the context import is declared in instance
// top.f, which is elaborated before top.l; the exports are in leaf instances
// at depth four below top.l, inside a loop generate and an instance array.
// Every one is reached by name after svSetScope, with its own state and
// parameter values.
//
// Expected trace, derived from the LRM (the calls run at time 1):
//   note 10 in top.l.lp[1].m[0].k (ID = 1): total 0 + 10 = 10
//   note 20 in top.l.lp[0].m[1].k (ID = 0): total 0 + 20 = 20
//   note 5 in top.l.lp[1].m[0].k: total 10 + 5 = 15 (state is per instance)
//   note 7 in top.l.lp[1].m[1].k (ID = 1): total 0 + 7 = 7
//   wait 3 in top.l.lp[0].m[1].k (ID = 0): time 1 + 3 = 4, status 0
`timescale 1ns/1ps
module top;
  first f ();
  late l ();
endmodule

module first;
  import "DPI-C" context task dpi_ord_run();
  initial begin
    #1;
    dpi_ord_run();
    $display("#c run done time=%0d", $time);
  end
endmodule

module late;
  for (genvar g = 0; g < 2; g++) begin : lp
    mid #(.ID(g)) m [2] ();
  end
endmodule

module mid #(parameter int ID = 0) ();
  leaf #(.ID(ID)) k ();
endmodule

module leaf #(parameter int ID = 0) ();
  export "DPI-C" function dpi_ord_note;
  export "DPI-C" task dpi_ord_wait;
  int total;
  function void dpi_ord_note(input int value);
    total += value;
    $display("#c note id=%0d value=%0d total=%0d", ID, value, total);
  endfunction
  task dpi_ord_wait(input int delay);
    #(delay);
    $display("#c wait id=%0d time=%0d", ID, $time);
  endtask
endmodule
