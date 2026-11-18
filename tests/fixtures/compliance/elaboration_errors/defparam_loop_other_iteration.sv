// fsim-expect: FSIM-ELAB-DEFPARAM-006 top=top
// fsim-expect-text: defparam 'lp[1].c.Q' in 'top.lp[0].z' changes a parameter outside that generate block or instance array element
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 23.10.1: each instantiation of a generate block is a
// separate hierarchy scope, so a defparam in one iteration of a loop
// generate may not target a parameter of another iteration.
module leaf #(parameter int Q = 1) ();
endmodule

module top;
  genvar i;
  for (i = 0; i < 2; i++) begin : lp
    leaf c();
    if (i == 0) begin : z
      defparam lp[1].c.Q = 7;
    end
  end
endmodule
