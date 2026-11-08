// SPDX-License-Identifier: Apache-2.0
//
// Writes through a dynamic index, blocking and nonblocking, to a variable
// that a VHDL signal reads (four_plane_vhdl.vhd): the write compiles inline
// in the v4 kernel also where the variable's storage holds the nine values
// (KOp::store_slot_dynamic, imm_b the width, sub 3 at once). IEEE 1800-2023
// 11.5.1: a write through an index sets that bit; 10.4.2: a nonblocking write
// takes effect after the active events, so the display of the same time step
// shows the value before it.
//   t=1: q[3] = 1          q = 00001000
//   t=2: q[6] = 1          q = 01001000; q[3] <= 0 pending, display shows
//                          01001000
//   t=3: the update applied q = 01000000
module svm(output reg [7:0] q);
  integer i;
  integer j;
  initial begin
    q = 8'h00;
    i = 3;
    j = 3;
    #1 q[i] = 1'b1;
    $display("inl p4 t1 q=%b", q);
    i = 6;
    #1 q[i] = 1'b1;
    q[j] <= 1'b0;
    $display("inl p4 t2 q=%b", q);
    #1 $display("inl p4 t3 q=%b", q);
  end
endmodule
