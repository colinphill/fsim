// fsim-expect: FSIM-ELAB-DEFPARAM-006 top=top
// fsim-expect-text: defparam 'u3.P' in 'top.g' changes a parameter outside that generate block or instance array element
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 23.10.1: a defparam in or under a generate block instance
// shall not change a parameter outside that hierarchy. The first name u3
// resolves upward (23.8) to the module's instance top.u3, outside g.
module sub #(parameter int P = 1) ();
endmodule

module top;
  sub u3();
  generate if (1) begin : g
    sub u2();
    defparam u3.P = 9;
  end endgenerate
endmodule
