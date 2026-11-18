// SPDX-License-Identifier: Apache-2.0
//
// $sformatf with a run-time format string in the v4 kernel's generated code
// (IEEE 1800-2023 21.3.3): packed (wide, signed, four-state), real, string
// and time arguments, %m, and a malformed format, which ends the run
// (FSIM-RUN-0001) on every engine.
module top;
  timeunit 1ns; timeprecision 1ns;
  string fmt, s, name;
  logic [79:0] wide;
  int signed neg;
  logic [7:0] four;
  real r;
  initial begin
    #3;
    wide = 80'h1234_5678_9abc_def0_1111;
    neg = -12;
    four = 8'b1x0z_0101;
    r = 2.5;
    name = "abc";
    fmt = "%h %0d %b";
    s = $sformatf(fmt, wide, neg, four);
    $display("inl packed %s", s);
    fmt = "%0.2f|%s|%5s";
    s = $sformatf(fmt, r, name, name);
    $display("inl real string %s", s);
    fmt = "%0t %0d";
    s = $sformatf(fmt, $time, $time);
    $display("inl time %s", s);
    fmt = "%m";
    s = $sformatf(fmt);
    $display("inl scope %s", s);
    fmt = "%q";
    s = $sformatf(fmt, neg);
    $display("inl malformed [%s]", s);
  end
endmodule
