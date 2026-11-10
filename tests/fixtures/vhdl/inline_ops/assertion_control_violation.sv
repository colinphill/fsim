// SPDX-License-Identifier: Apache-2.0
//
// The violation reports of unique, unique0 and priority case statements
// (IEEE 1800-2023 12.5.3) are assertion types 32, 64 and 128 of the
// assertion controls (20.11, Table 20-6): On, Off and Kill apply to them
// (the convenience tasks, of types 15, do not), and Lock keeps a control
// from them until Unlock. s is 3, which no item below matches. A report
// waits on the process's violation report queue until the Observed region
// (12.5.3.1), so each check is followed by `#1`: its report matures before
// the next control task runs. The reports, in order:
//
//   unique case       on                                 reported
//   $assertcontrol(4, 32): unique off
//   unique case       off                                not reported
//   priority case     on                                 reported
//   $assertoff: types 1, 2, 4 and 8 only
//   priority case     on                                 reported
//   Lock of type 128, then $assertcontrol(4): Off of every type
//   priority case     locked on                          reported
//   unique0 case      off                                not reported
//                     (two matching items)
//   Unlock of type 128, $assertcontrol(5, 128): Kill
//   priority case     off                                not reported
//   $assertcontrol(3): On of every type
//   unique case       on                                 reported
module top;
  logic [1:0] s = 2'd3;
  int hits;
  initial begin
    unique case (s) 2'd0: hits++; 2'd1: hits++; endcase #1;
    $assertcontrol(4, 32);
    unique case (s) 2'd0: hits++; endcase #1;
    priority case (s) 2'd0: hits++; endcase #1;
    $assertoff;
    priority case (s) 2'd0: hits++; endcase #1;
    $assertcontrol(1, 128);
    $assertcontrol(4);
    priority case (s) 2'd0: hits++; endcase #1;
    unique0 case (s) 2'd3: hits++; 2'd3: hits++; endcase #1;
    $assertcontrol(2, 128);
    $assertcontrol(5, 128);
    priority case (s) 2'd0: hits++; endcase #1;
    $assertcontrol(3);
    unique case (s) 2'd0: hits++; endcase #1;
  end
endmodule
