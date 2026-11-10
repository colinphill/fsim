// SPDX-License-Identifier: Apache-2.0
// A post_randomize that writes through a null handle (IEEE 1800-2023 8.4)
// cannot complete. That is an error of the run, not a randomization failure,
// which only an unsatisfiable solve is (18.6.1, 18.6.3): randomize() does
// not return 0, and the run stops naming the class, the callback and the
// cause.
class node;
  int v;
endclass

class item;
  rand bit [3:0] x;
  node link;

  function void post_randomize();
    link.v = x;
  endfunction
endclass

module top;
  item it;

  initial begin
    it = new;
    $display("randomize returned %0d", it.randomize());
  end
endmodule
