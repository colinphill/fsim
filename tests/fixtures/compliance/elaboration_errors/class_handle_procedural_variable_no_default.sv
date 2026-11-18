// fsim-expect: FSIM-SV-CLASS-033 top=top stage=compile
// fsim-expect-text: has no default specialization
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 8.25: the unadorned name of a parameterized class whose
// parameter has no default cannot type a procedural variable.
module top;
  class C #(int P);
    int v = P;
  endclass
  initial begin
    C h;
    $display("FAIL");
  end
endmodule
