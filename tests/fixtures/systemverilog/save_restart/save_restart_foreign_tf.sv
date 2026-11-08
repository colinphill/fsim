// SPDX-License-Identifier: Apache-2.0
// A PLI 1.0 library (misctf) across $save and a fresh-process restart (IEEE
// 1800-2023 38.9, 38.31, 38.36.3). clk toggles every 5 time units twenty times,
// so the run ends at t=100; $save at t=30 is taken at the end of that slot.
// misctf of $tf_probe (foreign_pli_tf_save_plugin.c, IEEE 1364-2001 21.1.4):
// calltf at t=1 tries tf_write_save outside reason_save (0). At the end of the
// slot of t=30 misctf hears reason_startofsave, then reason_save and writes
// two integers (1 = written). The restart run hears reason_startofrestart and
// reason_restart and reads 11,22 back (1 = read); calltf does not run again,
// because the restart continues after t=1.
`timescale 1ns/1ns
module top;
  reg clk;
  initial begin
    clk = 0;
    repeat (20) #5 clk = ~clk;
  end
  initial #1 $tf_probe;
  initial #30 $save("foreign.state");
endmodule
