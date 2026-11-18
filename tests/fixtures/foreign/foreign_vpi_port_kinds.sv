// SPDX-License-Identifier: Apache-2.0
// A port is a net or a variable as its own declaration says (IEEE 1800-2023
// 23.2.2.3), whatever the signal it shares in the instance above is: an
// output port written without a data type is a net of the default net type
// (22.8) even when its actual is a variable, an output port of a variable
// type is a variable, and an input port is a net.
module vpi_port_child(input logic a, output logic y, output z, output tri t);
  assign y = a;
  assign z = ~a;
  assign t = a;
endmodule

module top;
  logic l, cy, cz;
  wire ct;
  vpi_port_child c(.a(l), .y(cy), .z(cz), .t(ct));
  initial begin
    l = 1'b1;
    #2 $finish;
  end
endmodule
