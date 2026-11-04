// SPDX-License-Identifier: Apache-2.0
// Combinational blocks the kernel fuses into one partition; a VPI library
// drives the inputs and deposits into the output of one block. The value
// lasts while that block's own input is unchanged, also when another member
// of the partition runs (IEEE 1800-2023 38.34, 9.2.2.2).
module top (
    input  logic a,
    input  logic b,
    output logic y,
    output logic z
);
  logic w;
  always @(a) y = a;
  always @(b) z = b;
  always @(y or z) w = y ^ z;
  initial #30 $finish;
endmodule
