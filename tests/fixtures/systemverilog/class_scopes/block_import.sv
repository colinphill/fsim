// SPDX-License-Identifier: Apache-2.0
// A package import is a data declaration, so it may stand in a function or
// task body and in a begin-end block, and its names are visible there from the
// import on (IEEE 1800-2023 A.2.1.3, 26.3): a value, a function and a type.
package pf;
  localparam int K = 5;
  typedef bit [7:0] byte_t;
  function automatic int twice(int x);
    return x * 2;
  endfunction
endpackage

class Box;
  int base = 1;
  function int scaled();
    import pf::twice;
    import pf::K;
    return twice(K) + base;
  endfunction
  task automatic bump(output int result);
    import pf::K;
    result = K + base;
  endtask
endclass

module top;
  function automatic int total();
    import pf::*;
    byte_t b = 8'd200;
    return twice(K) + int'(b);
  endfunction
  Box x;
  int r;
  initial begin
    x = new;
    x.bump(r);
    $display("total=%0d scaled=%0d bump=%0d", total(), x.scaled(), r);
    begin
      import pf::K;
      $display("block K=%0d", K);
    end
  end
endmodule
