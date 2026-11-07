// SPDX-License-Identifier: Apache-2.0
// SystemVerilog reads a std_logic shared with VHDL in four states: the
// VHDL default U displays as x (IEEE 1800-2023 21.2.1.3). The order of the
// two languages' time-0 processes is unspecified, so the leaf samples
// between time steps and reports changes after time 0.
module svleaf(input logic p, output logic q);
  timeunit 1ns;
  timeprecision 100ps;
  assign q = ~p;
  initial #0.5 $display("sv p=%b q=%b", p, q);
  always @(p) if ($time > 0) $display("sv @(p) p=%b at %0t", p, $time);
endmodule
