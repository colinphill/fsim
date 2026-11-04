// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 access routines (IEEE 1364-2001 Clauses 22 and 23) over a small
// hierarchy: $acc_walk walks it, $acc_watch and $acc_unwatch add and delete
// a value change link on stim.
module leaf(input a, output y, inout io);
  wire w;
  reg r;
  parameter P = 5;
  assign w = a;
  not g1(y, w);
endmodule

module top;
  reg stim;
  wire out;
  wire io;
  reg [3:0] vec;
  integer count;
  parameter WIDTH = 4;
  leaf u1(.a(stim), .y(out), .io(io));
  initial begin
    stim = 0;
    vec = 4'b1010;
    count = 3;
    #1 $acc_walk;
    $acc_watch(stim);
    #1 stim = 1;
    // A change before the delete in the same step is reported, with its
    // value; the change after it is not (IEEE 1364-2001 23.97, 23.98).
    #1 stim = 0;
    $acc_unwatch(stim);
    stim = 1;
    #1 stim = 0;
    #1 $finish;
  end
endmodule
