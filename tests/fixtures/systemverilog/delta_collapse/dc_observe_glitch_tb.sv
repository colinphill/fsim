// SPDX-License-Identifier: Apache-2.0
module dc_observe_glitch_tb;
  logic a = 1'b0;
  wire q;
  int events = 0;
  dc_observe_glitch dut(.a(a), .q(q));
  always @(q) events++;
  initial begin
    repeat (6) #10 a = ~a;
    #10 $display("events=%0d", events);
    $finish;
  end
endmodule
