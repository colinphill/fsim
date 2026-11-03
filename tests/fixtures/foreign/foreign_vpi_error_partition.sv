// SPDX-License-Identifier: Apache-2.0
// A self-waking continuous assignment among continuous assignments the v4
// kernel may partition: once go rises at time 5 the loop never settles and
// exceeds the delta-cycle limit, a run-time error that ends the simulation.
module top;
  wire a, b, c;
  wire [3:0] v;
  reg go = 0;
  reg [3:0] x = 0;
  assign b = x[0] & x[1];
  assign c = b | x[2];
  assign a = go ? ~a : c;
  assign v = {a, b, c, x[3]};
  initial #5 go = 1;
endmodule
