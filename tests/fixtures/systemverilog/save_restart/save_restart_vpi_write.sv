// SPDX-License-Identifier: Apache-2.0
// A delayed vpi_put_value pending at $save, across a fresh-process restart
// (IEEE 1800-2023 38.34, D.9, 38.36.3). The plug-in
// (foreign_vpi_save_write_plugin.c) puts 42 on q at t=5 with a delay of 25, so
// the write is due at t=30; the design saves at t=10, at the end of that slot.
//
// saving run: the put at t=5; q is 0 at t=29 (before the write) and 42 at t=31
//   (after it) and at t=55.
// restart run: nothing of the library runs again (no start of simulation), so
//   the pending write comes from the file; the three displays are the same.
`timescale 1ns/1ns
module top;
  reg [7:0] q;
  initial begin
    q = 8'd0;
    #29 $display("#c t=%0d q=%0d", $time, q);
    #2 $display("#c t=%0d q=%0d", $time, q);
    #24 $display("#c t=%0d q=%0d", $time, q);
  end
  initial #10 $save("write.state");
endmodule
