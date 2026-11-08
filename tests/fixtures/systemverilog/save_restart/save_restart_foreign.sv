// SPDX-License-Identifier: Apache-2.0
// A foreign VPI library across $save and a fresh-process restart (IEEE
// 1800-2023 38.9, 38.31, 38.36.3). clk toggles every 5 time units twenty times,
// so the run ends at t=100; $save at t=30 is taken at the end of that slot.
// The plug-in's trace (foreign_vpi_save_session_plugin.c):
//
// saving run: cbStartOfSimulation, then at the end of the slot of t=30
//   cbStartOfSave (id 1, the first vpiSaveRestartID call, two puts of four
//   bytes, a save location), cbEndOfSave, and cbEndOfSimulation at t=100.
// restart run: no cbStartOfSimulation (a restart is not a start); the saved
//   registrations of cbStartOfRestart/cbEndOfRestart come back, the first
//   reads 7,9 from id 1 in two chunks of four bytes; the cbEndOfSimulation of
//   the library's startup routine is dropped (38.36.3) and the one registered
//   in cbEndOfRestart reports t=100 and "(after restart)".
`timescale 1ns/1ns
module top;
  reg clk;
  initial begin
    clk = 0;
    repeat (20) #5 clk = ~clk;
  end
  initial #30 $save("foreign.state");
endmodule
