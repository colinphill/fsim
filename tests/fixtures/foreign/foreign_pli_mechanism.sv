// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 association mechanism and misctf reasons (IEEE 1364-2001 21.1,
// Table 87, Table 88): the library registers nothing itself; $mech's misctf
// hears synch, rosynch, reactivate, force, release, disable and finish.
`timescale 1ns / 1ns
module top;
  reg [7:0] a;
  reg [7:0] b;
  reg [15:0] f;
  reg [15:0] g;
  initial begin
    a = 8'h00;
    b = 8'h00;
    f = $mech_fn(a);
    g = $mech_fn(b);
    #1 $mech(a, b);
    #1 force a = 8'h05;
    #1 release a;
    #1 force b = 8'h09;
    #1 release b;
    begin : blk
      #1 disable blk;
      $display("#c unreachable");
    end
    #6;
    $display("#c design a=%0h b=%0h f=%0h g=%0h", a, b, f, g);
    $finish;
  end
endmodule
