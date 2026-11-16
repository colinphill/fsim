// SPDX-License-Identifier: Apache-2.0
`include "inc/pkg.svh"
module top;
  import shared_pkg::*;
  child c();
  box b;
  initial begin
    b = new;
    $display("shared A=%0d %0d", twice(VALUE), b.get());
  end
endmodule
