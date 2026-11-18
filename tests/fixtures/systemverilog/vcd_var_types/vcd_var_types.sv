// SPDX-License-Identifier: Apache-2.0
//
// Each kind of net and variable is declared with its VCD var_type (IEEE
// 1800-2023 21.7.2.1): a net with its net type, an input port as a net
// (23.2.2.3), an output port without a data type as a net of the default
// net type even where it shares a variable of the instance above, a
// variable of a time, real or integer type with that keyword, and any other
// variable as reg.
module vcd_var_child(input logic a, output logic y, output z);
  assign y = a;
  assign z = ~a;
endmodule

module vcd_var_types;
  logic l;
  bit b;
  reg r;
  logic [3:0] v;
  wire w;
  tri tw;
  supply0 s0;
  wand wa;
  integer i;
  int n;
  byte by;
  longint li;
  time tm;
  real re;
  realtime rt;
  shortreal sr;
  logic cy, cz;
  vcd_var_child c(.a(l), .y(cy), .z(cz));
  assign w = l;
  assign tw = l;
  assign wa = l;
  initial begin
    $dumpfile("dump.vcd");
    $dumpvars;
    l = 0;
    b = 1;
    r = 1;
    v = 4'ha;
    i = 1;
    n = 2;
    by = 3;
    li = 4;
    tm = 5;
    re = 1.5;
    rt = 2.5;
    sr = 0.5;
    #1 l = 1;
    #1 $finish;
  end
endmodule
