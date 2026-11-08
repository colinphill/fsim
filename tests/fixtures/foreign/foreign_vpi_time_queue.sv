// SPDX-License-Identifier: Apache-2.0
// Time queue objects and the timed callbacks due in them through VPI (IEEE
// 1800-2023 37.80, 37.81, 38.36.2).
//
// The plug-in's probe runs at time 1, when the initial block is waiting for
// its `#5` (a timer at 5). It then registers cbAtStartOfSimTime at 8 and
// cbAfterDelay 20 (due at 1 + 20 = 21) and iterates the time queues: 5 with no
// callback, 8 with the start-of-time callback, 21 with the after-delay
// callback (37.81 details 1 and 2: increasing order of time).
`timescale 1ns/1ns
module top;
  logic a;
  initial begin
    a = 0;
    #5 a = 1;
    #5 a = 0;
    #20 $finish;
  end
endmodule
