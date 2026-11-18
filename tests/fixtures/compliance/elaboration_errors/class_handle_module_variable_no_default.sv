// fsim-expect: FSIM-SV-CLASS-033 top=top stage=compile
// fsim-expect-text: has no default specialization
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 8.25: a parameterized class whose parameter has no default
// has no default specialization, so its unadorned name cannot type a module
// variable either: every specialization overrides the parameters without
// defaults.
module top;
  class C #(int P);
    int v = P;
  endclass
  C h;
  initial $display("FAIL");
endmodule
