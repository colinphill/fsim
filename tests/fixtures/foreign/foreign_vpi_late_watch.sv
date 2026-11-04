// SPDX-License-Identifier: Apache-2.0
// An output a continuous assignment drives from an input only a VPI library
// writes; the library starts watching the output at time 5.
module top (
    input  logic [3:0] a,
    output logic [3:0] y
);
  assign y = a;
  initial #20 $finish;
endmodule
