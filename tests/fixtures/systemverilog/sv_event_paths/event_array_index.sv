// SPDX-License-Identifier: Apache-2.0
// Named-event array elements selected by a run-time index (IEEE 1800-2023
// 7.4, 15.5): a wait, a blocking trigger, a nonblocking trigger with and
// without a delay and the triggered state, on an ascending and a descending
// array. An index with an unknown bit or outside the declared range selects
// no element (7.4.6): its wait never resumes, its trigger does nothing and
// its triggered state is 0.
module top;
  event ev[2:5];
  event dv[7:4];
  int i, k, n;
  logic [3:0] u;
  initial begin
    k = 3;
    @(ev[k]);
    $display("sep idx wait ev[3] %0t", $time);
    @(dv[k + 3]);
    $display("sep idx wait dv[6] %0t", $time);
    @(ev[k + 1]);
    $display("sep idx wait ev[4] %0t", $time);
  end
  initial begin
    u = 4'bx1x0;
    n = 9;
    fork
      begin @(ev[u]); $display("sep idx unknown index resumed %0t", $time); end
      begin @(dv[n]); $display("sep idx out of range resumed %0t", $time); end
    join_none
  end
  always @(ev[2]) $display("sep idx ev[2] %0t", $time);
  initial begin
    #1;
    i = 2;
    -> ev[i];
    $display("sep idx triggered ev[2]=%0b ev[3]=%0b dv[9]=%0b ev[x]=%0b",
      ev[i].triggered, ev[i + 1].triggered, dv[n].triggered, ev[u].triggered);
    #1;
    i = 3;
    -> ev[i];
    -> dv[n];
    -> ev[u];
    #1;
    i = 6;
    ->> dv[i];
    #1;
    i = 4;
    ->> #2 ev[i];
    #3 $display("sep idx done %0t", $time);
  end
endmodule
