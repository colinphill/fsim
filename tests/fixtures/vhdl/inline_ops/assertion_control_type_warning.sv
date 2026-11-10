// SPDX-License-Identifier: Apache-2.0
//
// A $assertcontrol control_type that is not one of the values of IEEE
// 1800-2023 Table 20-5 (1 to 11) is reported with a warning, and the call
// has no effect: the run continues (20.11 gives only the valid values).
// So is a negative levels argument (20.11 and 21.7.1.2 count levels of 0
// or more), of $assertcontrol and of a convenience task.
// The controls name assertion type 32, the violation reports of unique
// case (Table 20-6); s is 3, which no item below matches, and a violation
// report matures in the Observed region of its time step (12.5.3.1), so
// each step is followed by `#1`. The reports, in order:
//
//   $assertcontrol(100 * one, 32)        warning, no effect
//   unique case                          reported
//   $assertcontrol(260 * one, 32)        warning; 260 is not Off (4)
//   unique case                          reported
//   $assertcontrol(0, 32), (-4, 32)      two warnings
//   unique case                          reported
//   $assertcontrol(4, 32): Off
//   unique case                          not reported
//   $assertcontrol(12, 32)               warning; not On
//   unique case                          not reported
//   $assertcontrol(3, 32, , -1)          warning; levels -1, not On
//   unique case                          not reported
//   $assertcontrol(3, 32, , 0): On
//   unique case                          reported
//   $assertcontrol(4, 32, , -2)          warning; levels -2, not Off
//   unique case                          reported
//   $asserton(-3)                        warning; no effect (and the
//                                        convenience tasks do not apply
//                                        to violation reports anyway)
//   unique case                          reported
module top;
  logic [1:0] s = 2'd3;
  int one = 1;
  int hits;
  initial begin
    $assertcontrol(100 * one, 32); #1;
    unique case (s) 2'd0: hits++; 2'd1: hits++; endcase #1;
    $assertcontrol(260 * one, 32); #1;
    unique case (s) 2'd0: hits++; 2'd1: hits++; endcase #1;
    $assertcontrol(0, 32); #1;
    $assertcontrol(-4, 32); #1;
    unique case (s) 2'd0: hits++; 2'd1: hits++; endcase #1;
    $assertcontrol(4, 32); #1;
    unique case (s) 2'd0: hits++; 2'd1: hits++; endcase #1;
    $assertcontrol(12, 32); #1;
    unique case (s) 2'd0: hits++; 2'd1: hits++; endcase #1;
    $assertcontrol(3, 32, , -1 * one); #1;
    unique case (s) 2'd0: hits++; 2'd1: hits++; endcase #1;
    $assertcontrol(3, 32, , 0); #1;
    unique case (s) 2'd0: hits++; 2'd1: hits++; endcase #1;
    $assertcontrol(4, 32, , -2); #1;
    unique case (s) 2'd0: hits++; 2'd1: hits++; endcase #1;
    $asserton(-3); #1;
    unique case (s) 2'd0: hits++; 2'd1: hits++; endcase #1;
    $finish;
  end
endmodule
