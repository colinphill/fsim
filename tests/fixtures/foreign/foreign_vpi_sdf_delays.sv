// SPDX-License-Identifier: Apache-2.0
// A design annotated with an SDF INTERCONNECT and a PORT delay, for the VPI
// intermodule path and delay device objects (IEEE 1800-2023 37.37, 37.45).
// The delay also delays the net: u1 sees the value of w that u0 drives after
// the delay, 6 ns to rise and 7 ns to fall once the plug-in has written the
// intermodule path (vpi_put_delays, 38.32), while u0 sees a at once.
`timescale 1ns/1ps
module drv(input logic a, output logic z);
  assign z = a;
  always @(a) if ($time >= 20) $display("#c net %m a=%b t=%0d", a, $time);
endmodule

module top;
  logic a = 1'b1;
  wire w, y;
  drv u0(.a(a), .z(w));
  drv u1(.a(w), .z(y));
  // A PORT delay then an INTERCONNECT delay to the same load: the later
  // record applies (IEEE 1364-2005 16.3), 2 ns to rise and 3 ns to fall.
  logic b = 1'b1;
  wire w2, y2;
  drv u2(.a(b), .z(w2));
  drv u3(.a(w2), .z(y2));
  initial begin
    #25 b = 1'b0;
    #20 b = 1'b1;
  end
  initial begin
    #20 a = 1'b0;
    #20 a = 1'b1;
    #30 $finish;
  end
endmodule
