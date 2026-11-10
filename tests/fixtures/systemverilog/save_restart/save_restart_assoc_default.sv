// SPDX-License-Identifier: Apache-2.0
// `--save-at` and `--restart` (IEEE 1800-2023 D.9) of associative arrays
// with a default (7.9.11, 7.8.6): a read of a key with no entry returns the
// default, in the saving run and after a restart. Expected output, derived
// by hand:
//
// a and s have defaults from their declarations, 7 and "d"; b gets the
// default 6 from the assignment at t=10, before the save at t=50, so only the
// saved state holds it. At t=5 a[9] has no entry: 7. At t=60 a holds 3:30
// (a[9] is 7), s holds "k":"v" (s["z"] is "d") and b holds 1:1 (b[9] is 6).
// The saving run prints both lines (save_restart_assoc_default.expected); a
// restart from the state saved at t=50 prints the t=60 line
// (save_restart_assoc_default.restart.expected).
`timescale 1ns/1ns
module top;
  int a[int] = '{3: 30, default: 7};
  string s[string] = '{default: "d"};
  int b[int];
  initial begin
    #5 $display("t=%0d a=%0d", $time, a[9]);
    #5 b = '{1: 1, default: 6};
    s["k"] = "v";
    #50 $display("t=%0d a=%0d,%0d,%0d s=%0d,%s,%s b=%0d,%0d,%0d", $time,
      a.num(), a[3], a[9], s.num(), s["k"], s["z"], b.num(), b[1], b[9]);
  end
endmodule
