// fsim-expect: FSIM-ELAB-068 top=top
// fsim-expect-text: compatible direction
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 11.5.1: a constant part-select's range follows the
// direction of the vector's declared range; `a[0:3]` of a descending
// `logic [7:0]` is illegal, in an instance below the root as at the root.
module child #(parameter int L = 0, parameter int R = 3);
  logic [7:0] a;
  logic [3:0] r;
  initial r = a[L:R];
endmodule
module top;
  child u();
endmodule
