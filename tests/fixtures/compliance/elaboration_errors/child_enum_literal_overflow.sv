// fsim-expect: FSIM-ELAB-SVENUM-001 top=top
// fsim-expect-text: enum literal 'A' does not fit the base type of 'e_t'
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.19, 23.10: an instance's parameter values give its
// declarations their meaning; with W = 2 the base type of e_t is
// logic [1:0], and the literal value 5 does not fit it. The rule holds in
// an instance below the root as at the root.
module child #(parameter int W = 8);
  typedef enum logic [W-1:0] { A = 5, B } e_t;
  e_t e;
  initial $display("%0d", e);
endmodule
module top;
  child #(.W(2)) u();
endmodule
