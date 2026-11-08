// SPDX-License-Identifier: Apache-2.0
// Assertion callbacks on immediate assertions (IEEE 1800-2023 39.4.1). a_ok
// passes at 5 ns (v is still 0) and a_bad fails at 10 ns (v became 1), whose
// else action prints after the failure callback.
`timescale 1ns/1ns
module top;
  reg v;
  initial begin
    v = 0;
    #5 a_ok: assert (v == 0);
    #5 v = 1;
    a_bad: assert (v == 0) else $display("#c action");
    #5 $finish;
  end
endmodule
