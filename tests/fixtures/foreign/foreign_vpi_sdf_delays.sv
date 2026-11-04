// SPDX-License-Identifier: Apache-2.0
// A design annotated with an SDF INTERCONNECT and a PORT delay, for the VPI
// intermodule path and delay device objects (IEEE 1800-2023 37.37, 37.45).
`timescale 1ns/1ps
module drv(input logic a, output logic z);
  assign z = a;
endmodule

module top;
  logic a = 1'b1;
  wire w, y;
  drv u0(.a(a), .z(w));
  drv u1(.a(w), .z(y));
  initial #10 $finish;
endmodule
