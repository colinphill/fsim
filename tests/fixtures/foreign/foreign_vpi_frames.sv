// SPDX-License-Identifier: Apache-2.0
// Frames, threads and cbCreateObj through VPI (IEEE 1800-2023 37.43, 37.44,
// 38.36.1).
module top;
  class Item;
    int v;
    function new(int x);
      v = x;
    endfunction
  endclass
  Item it, cp;
  int n;
  function automatic int twice(int x);
    return x * 2;
  endfunction
  task automatic inner(int x);
    n = n + twice(x);
  endtask
  task automatic outer;
    inner(3);
  endtask
  initial begin : main
    n = 0;
    outer;
    fork
      #1 n = n + 100;
    join
    it = new(5);
    // The copy is a class object too (38.36.1 cbCreateObj).
    cp = new it;
    #1 $display("#c done n=%0d v=%0d", n, it.v);
  end
endmodule
