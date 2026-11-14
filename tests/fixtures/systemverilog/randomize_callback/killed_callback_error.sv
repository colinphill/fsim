// SPDX-License-Identifier: Apache-2.0
// A pre_randomize that kills its own process (IEEE 1800-2023 9.7) ends that
// process there, inside the callback. A later run error of another process
// is not an error of that callback (18.6.1): it names no callback, only its
// cause, a write through a null handle (8.4).
class node;
  int v;
endclass

class item;
  rand bit [3:0] x;

  function void pre_randomize();
    process p = process::self();
    $display("pre_randomize kills its process");
    p.kill();
  endfunction
endclass

module top;
  item it;
  node link;

  initial begin
    it = new;
    fork
      begin
        $display("randomize returned %0d", it.randomize());
      end
    join_none
    #1;
    $display("after the killed callback");
    link.v = 1;
  end
endmodule
