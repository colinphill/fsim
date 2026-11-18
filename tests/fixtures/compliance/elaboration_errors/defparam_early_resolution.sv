// fsim-expect: FSIM-ELAB-DEFPARAM-007 top=m
// fsim-expect-text: defparam 'm.n.p' resolved to 'm.n' before the hierarchy was elaborated but resolves to 'm.n.m.n' in the elaborated hierarchy
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 23.10.4.2 (the example of the clause): the defparam is
// resolved before the conditional generate, to m1's own p, which makes the
// condition true; in the elaborated hierarchy `m.n.p` names the parameter
// of m2 in generate block m instead, which is an error.
module m;
  m1 n();
endmodule

module m1;
  parameter p = 2;
  defparam m.n.p = 1;
  generate
    if (p == 1) begin : m
      m2 n();
    end
  endgenerate
endmodule

module m2;
  parameter p = 3;
endmodule
