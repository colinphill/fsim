// SPDX-License-Identifier: Apache-2.0
// LRM 6.6.7 struct nettype Tsum
module top;
  typedef struct { real field1; bit field2; } T;
  function automatic T Tsum (input T driver[]);
    Tsum.field1 = 0.0;
    foreach (driver[i]) Tsum.field1 += driver[i].field1;
  endfunction
  nettype T wT;
  nettype T wTsum with Tsum;
  nettype wTsum nettypeid2;
  wTsum w3;
  nettypeid2 w4;
  wT w1;
  T v1 = '{1.5, 1}, v2 = '{2.25, 0};
  assign w3 = v1;
  assign w3 = v2;
  assign w4 = v1;
  assign w1 = v2;
  initial #1 $display("w3=%f w4=%f w1=%f/%0d", w3.field1, w4.field1, w1.field1, w1.field2);
endmodule
