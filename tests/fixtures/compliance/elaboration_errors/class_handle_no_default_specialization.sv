// fsim-expect: FSIM-SV-CLASS-033 top=top stage=compile
// fsim-expect-text: parameterized class-handle type 'work::top::C' has no actual-association record
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 8.25: a parameterized class whose parameter has no default
// has no default specialization, so its unadorned name cannot type a class
// property: every specialization overrides the parameters without defaults.
module top;
  class C #(int P);
    int v = P;
  endclass
  class Holder;
    C h;
  endclass
  Holder x;
  initial $display("FAIL");
endmodule
