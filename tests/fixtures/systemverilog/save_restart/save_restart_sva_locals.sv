// SPDX-License-Identifier: Apache-2.0
// A save of a run whose concurrent assertions have local variables (IEEE
// 1800-2023 16.10). clk rises at 5, 15, 25, ...; v is high at the edges of
// 15 and 25, when d is 7 and 9, and q is d + 1 three edges later except at
// 55, where it is 0. p1 takes x = d at each v and expects q == x + 1 at the
// third edge after: the attempt of 15 passes at 45, the one of 25 fails at
// 55. p2 also has disable iff (rst), and rst pulses from 48 to 50, between
// the edges: its attempt of 25, in flight, is disabled at 48 (16.12).
//
// The run is saved at 30, with both attempts in flight and their locals
// held by the monitor's threads; the restart holds the same values and ends
// with the same output.
`timescale 1ns/1ns
module top;
  reg clk;
  reg v;
  reg rst;
  int d;
  int q;
  initial begin
    clk = 0;
    v = 0;
    d = 0;
    q = 0;
    rst = 0;
    repeat (14) #5 clk = ~clk;
  end
  initial begin
    #12 v = 1;
    d = 7;
    #10 d = 9;
    #10 v = 0;
    d = 0;
    #10 q = 8;
    #10 q = 0;
  end
  initial begin
    #48 rst = 1;
    #2 rst = 0;
  end
  property p(bit dis);
    int x;
    @(posedge clk) disable iff (dis) (v, x = d) |-> ##3 q == x + 1;
  endproperty
  p1: assert property (p(1'b0))
    else $display("p1 fail t=%0d", $time);
  p2: assert property (p(rst))
    else $display("p2 fail t=%0d", $time);
  initial #60 $display("done t=%0d", $time);
endmodule
