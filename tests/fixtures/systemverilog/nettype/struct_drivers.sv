// SPDX-License-Identifier: Apache-2.0
// a real-member struct nettype: the drivers its resolution function sees
module top;
  typedef struct { real field1; bit field2; } T;
  function automatic T Tsum (input T driver[]);
    $display("size=%0d", driver.size());
    foreach (driver[i]) $display("  d[%0d]=%f", i, driver[i].field1);
    Tsum.field1 = 0.0;
    foreach (driver[i]) Tsum.field1 += driver[i].field1;
  endfunction
  nettype T wTsum with Tsum;
  wTsum w3;
  T v1 = '{1.5, 1}, v2 = '{2.25, 0};
  assign w3 = v1;
  assign w3 = v2;
  initial #1 $display("w3=%f", w3.field1);
endmodule
