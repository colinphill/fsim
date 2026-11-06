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
  reg [3:0] av;
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
    // A procedural assign overrides later procedural writes until the
    // deassign; a force wins over it and its release reveals the assigned
    // value again; the deassign leaves the value until the next procedural
    // assignment (IEEE 1364-2005 9.3.1, 9.3.2, 23.96).
    av = 4'b0000;
    $acc_av(av, 1);
    av = 4'b1111;
    $acc_av(av, 5);
    $acc_av(av, 2);
    $acc_av(av, 3);
    av = 4'b0011;
    $acc_av(av, 5);
    $acc_av(av, 4);
    $acc_av(av, 5);
    av = 4'b0011;
    $acc_av(av, 5);
    // A force hides procedural writes from every reader, the process's own
    // included; the release leaves a variable at the forced value until its
    // next procedural assignment (IEEE 1800-2023 10.6.2, 1364-2005 9.3.2).
    av = 4'b0000;
    $acc_av(av, 2);
    av = 4'b1111;
    $acc_av(av, 5); $display("#c d av=%b", av);
    $acc_av(av, 3);
    $acc_av(av, 5); $display("#c d av=%b", av);
    av = 4'b1010;
    $acc_av(av, 5); $display("#c d av=%b", av);
    #1 $finish;
  end
endmodule
