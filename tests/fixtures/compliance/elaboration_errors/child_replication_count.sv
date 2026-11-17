// fsim-expect: FSIM-ELAB-SVREPL-001 top=top
// fsim-expect-text: a replication concatenation requires a nonnegative constant count
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 11.4.12, 23.10: a replication count is a nonnegative
// constant; the instance overrides N with -1, so the replication in the
// child is illegal there.
module child #(parameter int N = 1);
  logic [3:0] a;
  logic [7:0] r;
  initial r = {N{a}};
endmodule
module top;
  child #(.N(-1)) u();
endmodule
