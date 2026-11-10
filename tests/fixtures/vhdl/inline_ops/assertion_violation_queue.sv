// SPDX-License-Identifier: Apache-2.0
//
// The violation reports of unique, unique0 and priority if (IEEE 1800-2023
// 12.4.2) and case (12.5.3) statements. The qualifier of an if applies to
// its whole series of if-else-if conditions; a begin-end around a nested if
// ends the series. unique and unique0 report two true conditions, unique
// and priority report none true without a final else. A violation is
// checked as the statement executes and its report waits on the process's
// violation report queue until the Observed region (12.4.2.1, 12.5.3.1): a
// flush point -- the process resuming at its event control, an always_comb
// woken by a dependency -- discards the pending reports, and so does a Kill
// of their type, which an Off does not (20.11, types 32, 64 and 128).
//
// The checks of the always @(s) process, by value of s:
//
//   u1  unique   (s == 1) / s[0]              0: none  1: both  2: none
//   u0  unique0  (s == 2) / s[1]              2: both
//   p1  priority (s == 3) / (s == 1)          0: none  2: none
//   p2  priority s[0] / s[1] / else           never
//   u2  unique   (s == 0) / else begin if     never (a series of one)
//   u3  unique   (s == 3) / s[0] / (s != 0)   0: none  1: last two  3: all
//   c   unique case 1, 2                      0: none  3: none
//
//   t1  s = 1                    u1 multiple, u3 multiple
//   t2  s = 2                    u1 none, u0 multiple, p1 none
//   t3  s = 0, #0 s = 3          the s = 0 reports are flushed when the
//                                process resumes; s = 3: u3 multiple, c none
//   t4  s = 2, #0 Kill of 32     u1 none flushed; u0 multiple, p1 none
//   t5  On of 32
//   t6  s = 1, #0 Off of 32      u1 multiple, u3 multiple (an Off does not
//                                flush)
//   t7  s = 0                    u1, u3 and c off; p1 none
//   t8  On of 32, s = 3          u3 multiple, c none
//   t9  a = 1, t10 a = 0         the glitch of 12.4.2.1: never reported
module top;
  logic [1:0] s = 2'd0;
  logic a = 1'b0;
  logic not_a;
  int r;
  int z;
  always @(s) begin
    /* u1 */ unique if (s == 2'd1) r = 1; else if (s[0]) r = 2;
    /* u0 */ unique0 if (s == 2'd2) r = 3; else if (s[1]) r = 4;
    /* p1 */ priority if (s == 2'd3) r = 5; else if (s == 2'd1) r = 6;
    /* p2 */ priority if (s[0]) r = 7; else if (s[1]) r = 8; else r = 9;
    /* u2 */ unique if (s == 2'd0) r = 10; else begin if (s == 2'd1) r = 11; end
    /* u3 */ unique if (s == 2'd3) r = 12;
    else if (s[0]) r = 13;
    else if (s != 2'd0) r = 14;
    /* c */ unique case (s) 2'd1: r = 15; 2'd2: r = 16; endcase
  end
  // IEEE 1800-2023 12.4.2.1: a1 may run while a and not_a are both 1 (or,
  // at time 0, both not 1), but it runs again once not_a follows a.
  always_comb not_a = !a;
  always_comb begin : a1
    unique if (a) z = 1;
    else if (not_a) z = 2;
  end
  initial begin
    #1 s = 2'd1;
    #1 s = 2'd2;
    #1 s = 2'd0;
    #0 s = 2'd3;
    #1 s = 2'd2;
    #0 $assertcontrol(5, 32);
    #1 $assertcontrol(3, 32);
    #1 s = 2'd1;
    #0 $assertcontrol(4, 32);
    #1 s = 2'd0;
    #1 $assertcontrol(3, 32);
    s = 2'd3;
    #1 a = 1'b1;
    #1 a = 1'b0;
  end
endmodule
