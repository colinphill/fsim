// fsim-expect: FSIM-ELAB-SVCAST-003 top=top
// fsim-expect-text: a SystemVerilog casting size must be a positive, supported integral width
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.24.1, 23.10: a size cast's constant must be positive;
// the instance overrides P with 0, so `P'(a)` in the child is illegal there.
module child #(parameter int P = 4);
  logic [7:0] a, r;
  initial r = P'(a);
endmodule
module top;
  child #(.P(0)) u();
endmodule
