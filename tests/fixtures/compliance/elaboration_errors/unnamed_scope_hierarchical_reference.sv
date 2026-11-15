// fsim-expect: FSIM-ELAB-ROOT-001 top=top
// fsim-expect-text: cannot be referenced hierarchically (IEEE 1800-2023 6.21, 9.3.5)
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 9.3.5: an item declared in an unnamed block that declares
// items, or in a named block within one, cannot be referenced
// hierarchically; `step` is under the unnamed begin of the procedure.
module top;
  initial begin
    int i;
    begin : step
      logic y;
      y = 1'b1;
    end
  end
  initial #1 $display("y=%b", top.step.y);
endmodule
