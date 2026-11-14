// SPDX-License-Identifier: Apache-2.0
// Module directive state and defaults (IEEE 1800-2023 37.5): the condition
// of `default disable iff` (16.15, vpiDefaultDisableIff) and the argument of
// `default_decay_time (E.2, vpiDefDecayTime).
`default_decay_time 100
module with_disable(input logic clk, input logic rst);
  default disable iff (rst);
  logic a = 1;
endmodule
`default_decay_time 2.5
module real_decay;
  logic a;
endmodule
`default_decay_time infinite
module no_decay;
  logic a;
endmodule
module top;
  logic clk = 0;
  logic rst = 1;
  with_disable u_dis(.clk(clk), .rst(rst));
  real_decay u_real();
  no_decay u_inf();
endmodule
