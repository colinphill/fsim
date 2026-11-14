// SPDX-License-Identifier: Apache-2.0
//
// A nonblocking assignment to an element of a container declared in a
// procedural block (IEEE 1800-2023 6.21, 10.4.2): the variable is static in a
// module's initial block (or when declared static), so the element is sampled
// at the statement and written in the NBA region; of two assignments the later
// wins and an intra-assignment delay moves the write to the later time slot.
module top;
  initial begin : blk
    int sq[$];
    static int sa[int];
    static string sm[string];
    int fixed[3];
    sq.push_back(1);
    sq.push_back(2);
    sq[0] <= 10;
    sq[1] <= 20;
    sq[1] <= 21;
    sa[3] <= 30;
    sm["k"] <= "kay";
    fixed[1] <= 5;
    fixed[2] <= #1 6;
    $display("sns proc same-step %0d %0d %0d n=%0d %0d %0d", sq[0], sq[1], sa.num(), sm.num(), fixed[1], fixed[2]);
    #1;
    $display("sns proc next-step %0d %0d %0d %s %0d %0d", sq[0], sq[1], sa[3], sm["k"], fixed[1], fixed[2]);
    #1;
    $display("sns proc last %0d", fixed[2]);
  end
endmodule
