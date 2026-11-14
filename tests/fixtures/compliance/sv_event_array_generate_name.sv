// fsim: top=sv_event_array_generate_name std=2017
// SPDX-License-Identifier: Apache-2.0
// An element of an event array declared in a named conditional generate block,
// named hierarchically from outside the block (IEEE 1800-2023 7.4, 15.5,
// 23.6): `@(g.ge[8])`, `wait (g.ge[k].triggered)` with a variable index and
// `-> g.ge[8]` reach the block's own events, and the other elements stay
// untriggered.
module sv_event_array_generate_name;
  generate if (1) begin : g
    event ge[10];
  end endgenerate
  integer hits = 0;
  integer k = 3;
  initial begin
    @(g.ge[8]);
    if ($time != 1)
      $fatal(1, "@(g.ge[8]) woke at %0t, expected 1", $time);
    hits = hits + 1;
  end
  initial begin
    wait (g.ge[k].triggered);
    if ($time != 2)
      $fatal(1, "g.ge[k].triggered held at %0t, expected 2", $time);
    hits = hits + 1;
  end
  initial begin
    @(g.ge[5]);
    $fatal(1, "an untriggered element g.ge[5] woke at %0t", $time);
  end
  initial begin
    #1 -> g.ge[8];
    #1 -> g.ge[3];
    #1;
    if (hits != 2)
      $fatal(1, "hits=%0d, expected 2", hits);
    $display("PASS");
    $finish;
  end
endmodule
