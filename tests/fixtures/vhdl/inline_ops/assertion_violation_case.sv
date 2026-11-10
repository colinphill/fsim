// SPDX-License-Identifier: Apache-2.0
//
// The violation reports of the unique, unique0 and priority forms of case,
// casex, casez, case inside and case matches (IEEE 1800-2023 12.5.3,
// 12.6.1). Each check runs in its own time step: its report waits on the
// process's violation report queue and matures in the Observed region
// (12.5.3.1, 12.4.2.1), before the process resumes after the delay.
//
//   1  unique case, two items match           multiple
//   2  unique case, no item matches           none
//   3  unique0 casex, two items match         multiple
//   4  unique0 case, no item matches          (no report: unique0)
//   5  priority casez, no item matches        none
//   6  priority case, two items match         (no report: the first wins)
//   7  unique case inside, two items match    multiple
//   8  unique case matches, two items match   multiple
//   9  priority case matches, none matches    none
//  10  unique case with a default, none       (no report: the default)
module top;
  logic [1:0] selector = 2'b01;
  int r;
  initial begin
    #1 unique case (selector)
      2'b01: r = 1;
      2'b01: r = 2;
    endcase
    #1 unique case (selector)
      2'b11: r = 3;
    endcase
    #1 unique0 casex (2'bx1)
      2'b01: r = 4;
      2'b11: r = 5;
    endcase
    #1 unique0 case (selector)
      2'b10: r = 6;
    endcase
    #1 priority casez (2'b11)
      2'b00: r = 7;
    endcase
    #1 priority case (selector)
      2'b01: r = 8;
      2'b01: r = 9;
    endcase
    #1 unique case (selector) inside
      2'b01: r = 10;
      [2'b00:2'b10]: r = 11;
    endcase
    #1 unique case (selector) matches
      2'b01: r = 12;
      .*: r = 13;
    endcase
    #1 priority case (2'b11) matches
      2'b00: r = 14;
    endcase
    #1 unique case (selector)
      2'b10: r = 15;
      default: r = 16;
    endcase
    #1 $display("r=%0d", r);
  end
endmodule
