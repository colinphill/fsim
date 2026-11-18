// SPDX-License-Identifier: Apache-2.0
//
// Force and release in the v4 kernel's generated code (IEEE 1800-2023
// 10.6.2): a forced variable ignores procedural writes and keeps the forced
// value after release until the next one; a forced part-select of a net
// shows the forced bits while the rest follows its driver, and a process
// sensitive to the net sees each change; release reveals the driver.
module top;
  logic [3:0] v;
  logic [7:0] d;
  wire [7:0] n = d;
  int changes;
  always @(n) changes++;
  initial begin
    v = 1;
    d = 8'h11;
    // Counted from here: the force, the driver's change and the release.
    #1 changes = 0;
    force v = 5;
    force n[7:4] = 4'ha;
    #1 v = 9;
    d = 8'h22;
    #1 $display("inl forced v=%0d n=%h", v, n);
    release v;
    release n[7:4];
    #0 $display("inl released v=%0d n=%h", v, n);
    #1 v = 3;
    #1 $display("inl assigned v=%0d changes=%0d", v, changes);
  end
endmodule
