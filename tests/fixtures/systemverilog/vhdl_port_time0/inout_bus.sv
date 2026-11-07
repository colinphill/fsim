// SPDX-License-Identifier: Apache-2.0
// A 72-bit SystemVerilog net on a VHDL inout port with SystemVerilog drivers
// of its own: a slice across the first word boundary and a single bit,
// against the VHDL driver's weak and strong values (resolved in the kernel;
// docs/cross-language-semantics.md).
module top;
  timeunit 1ns;
  timeprecision 100ps;
  wire [71:0] bus_io;
  logic en = 1'b0;
  logic [7:0] value = 8'h3c;
  assign bus_io[67:60] = en ? value : 8'bz;
  assign bus_io[2] = en;
  inout_bus_leaf u(.bus_io(bus_io));
  initial begin
    #0.1;
    forever begin
      #1.5 en = ~en;
      value = value + 8'h11;
    end
  end
  initial begin
    #0.5;
    for (int step = 0; step < 11; ++step) begin
      $display("sv %0d bus=%b", step, bus_io);
      #1;
    end
    $finish;
  end
endmodule
