// SPDX-License-Identifier: Apache-2.0
// A package variable whose initializer is not a literal runs once on the
// reset's twin (IEEE 1800-2023 D.8: the variables take their initial values),
// with output discarded; the run takes the result and prints nothing for it
// (owner decision 2026-11-07). Derivation:
//
//  life 1: the package initializer f() runs before any process and prints
//          "pkg init"; v is 5. The initial block prints count 0, v=5, sets
//          v = 9, waits 1 ns and calls $reset(1).
//  reset:  the twin runs f() again, silently; v is 5 again, $reset_count 1.
//  life 2: the initial block prints count 1, v=5 (the 9 is gone) and does not
//          reset again. "pkg init" is not printed a second time.
`timescale 1ns/1ns
package p;
  function automatic int f();
    $display("pkg init");
    return 5;
  endfunction
  int v = f();
endpackage
module top;
  import p::*;
  initial begin
    $display("life count=%0d v=%0d", $reset_count, v);
    v = 9;
    if ($reset_count == 0) begin
      #1;
      $reset(1);
    end
  end
endmodule
