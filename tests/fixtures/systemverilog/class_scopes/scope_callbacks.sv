// SPDX-License-Identifier: Apache-2.0
// pre_randomize, post_randomize and methods of a class declared in a module
// or in a generate block run as the class of that design element instance or
// generate iteration, and read its parameters and genvars (IEEE 1800-2023
// 6.22, 8.2, 18.6.1, 27.4).
module top;
  parameter int P = 5;
  class mc;
    rand bit [3:0] x;
    int n;
    function void pre_randomize(); n = P; endfunction
    function int get(); return n + P; endfunction
  endclass
  for (genvar g = 0; g < 2; g++) begin : gb
    class gc;
      rand bit [3:0] y;
      int m;
      function void post_randomize(); m = g + 10; endfunction
      function int val(); return m * 2 + g; endfunction
    endclass
    gc o;
    initial begin
      #(g + 1);
      o = new;
      void'(o.randomize());
      $display("gen %0d m=%0d val=%0d", g, o.m, o.val());
    end
  end
  mc h;
  initial begin
    h = new;
    void'(h.randomize());
    $display("mod n=%0d get=%0d", h.n, h.get());
  end
endmodule
