// SPDX-License-Identifier: Apache-2.0
// Named-event arrays (IEEE 1800-2023 7.4, 15.5): an array whose element type
// is a typedef of event, a typedef of the whole array, an array of more than
// 65,536 events (no implementation bound), and arrays of generate blocks
// selected hierarchically with constant and variable indices (23.6, 23.8).
module top;
  typedef event ev_t;
  typedef event ev4_t[4];
  ev_t e1[3];
  ev4_t e2;
  event big[70000];
  for (genvar k = 0; k < 2; k++) begin : g
    event ge[10];
    initial begin
      @(ge[8]);
      $display("sep g[%0d].ge[8] %0t", k, $time);
    end
  end
  int i, j;
  initial begin
    fork
      begin @(e1[1]); $display("sep e1[1] %0t", $time); end
      begin @(e2[i]); $display("sep e2[%0d] %0t", i, $time); end
      begin @(big[69999]); $display("sep big[69999] %0t", $time); end
      begin @(g[1].ge[j]); $display("sep top g[1].ge[%0d] %0t", j, $time); end
    join
    $display("sep joined %0t", $time);
  end
  initial begin
    i = 2;
    j = 7;
    #1 -> e1[1];
    #1 -> e2[i];
    #1 -> big[69999];
    #1 -> g[0].ge[8];
    #1 -> g[1].ge[j];
    #1 -> g[1].ge[8];
  end
endmodule
