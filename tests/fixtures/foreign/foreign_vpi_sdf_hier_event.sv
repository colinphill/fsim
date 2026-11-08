// SPDX-License-Identifier: Apache-2.0
// An event control through the name of a load port waits on the port's delayed
// copy (IEEE 1364-2005 16.2.4: a reference to an annotated port, wherever it
// occurs, provides the delayed signal value), also when the value it reads is
// a select of the port. The SDF PORT delays are 3 ns, so the events happen 3
// ns after the net changes: the net changes at 10, 30 and 50.
// An index or select of the name in the event control waits on the copy too
// (`@(u1.w[1])`, `@(posedge u1.bus[2])`): w changes 00 -> 01 at 60 (bit 1
// keeps its value: no event) and 01 -> 11 at 70 (bit 1 rises: the copy's bit
// changes at 73); bus changes 000 -> 011 at 80 (bit 2 stays 0) and 011 -> 111
// at 90 (bit 2 rises: posedge at 93), then falls at 100 (no posedge).
`timescale 1ns/1ps
module leaf(input wire a, input wire [1:0] v, input wire [1:0] w,
  input wire [2:0] bus);
endmodule

module top;
  logic s = 1'b0;
  logic [1:0] s2 = 2'b00;
  logic [1:0] s4 = 2'b00;
  logic [2:0] s5 = 3'b000;
  leaf u1(.a(s), .v(s2), .w(s4), .bus(s5));
  always @(u1.a) $display("#c ev t=%0d a=%b", $time, u1.a);
  always @(u1.v) $display("#c ev2 t=%0d v1=%b", $time, u1.v[1]);
  always @(u1.w[1]) $display("#c ev3 t=%0d w1=%b", $time, u1.w[1]);
  always @(posedge u1.bus[2]) $display("#c ev4 t=%0d b2=%b", $time, u1.bus[2]);
  initial begin
    #10 s = 1'b1;
    #20 s = 1'b0;
    #20 s2 = 2'b10;
    #10 s4 = 2'b01;
    #10 s4 = 2'b11;
    #10 s5 = 3'b011;
    #10 s5 = 3'b111;
    #10 s5 = 3'b000;
    #20 $finish;
  end
endmodule
