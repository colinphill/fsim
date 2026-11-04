// SPDX-License-Identifier: Apache-2.0
// DPI time and scope service (IEEE 1800-2023 35.5.3, H.9.3, H.13): two
// instances with different time scales call one context import.
`timescale 1ns/1ps
import "DPI-C" context function void dpi_time_scope_probe(input int id);

module top;
  a u0 ();
  b u1 ();
endmodule

module a;
  initial begin
    #5;
    dpi_time_scope_probe(0);
  end
endmodule

`timescale 1us/1ns
module b;
  initial begin
    #2;
    dpi_time_scope_probe(1);
  end
endmodule
