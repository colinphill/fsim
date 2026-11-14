// SPDX-License-Identifier: Apache-2.0
// Foreign writes of nets of a user-defined nettype (IEEE 1800-2023 6.6.7,
// 38.34): the plug-in (foreign_vpi_nettype_put_plugin.c) deposits on, forces
// and releases a resolved net, an element of an array of them and an element
// of an array of real nettype nets (held whole by one signal). A deposit
// holds until the net's next resolution (a driver change); a force holds
// over driver changes; a release returns the resolved value of the drivers.
module top;
  function automatic int rsum(input int d[]);
    rsum = 0;
    foreach (d[i]) rsum += d[i];
  endfunction
  function automatic real ravg(input real d[]);
    ravg = 0.0;
    foreach (d[i]) ravg += d[i];
    if (d.size() > 0) ravg = ravg / d.size();
  endfunction
  nettype int wsum with rsum;
  nettype real wavg with ravg;
  wsum n;
  wsum a[2];
  wavg r[2];
  int x = 1, y = 2, x2 = 1, y2 = 2;
  real p = 1.0, q = 2.0;
  assign n = x;
  assign n = y;
  assign a[1] = x2;
  assign a[1] = y2;
  assign r[1] = p;
  assign r[1] = q;
  // The three nets change at different times, so the order of these
  // displays is defined (IEEE 1800-2023 4.7 leaves the order of processes
  // woken together unspecified); at time zero only n is shown.
  always @(n) $display("#c %0t n=%0d", $time, n);
  always @(a[1]) if ($time != 0) $display("#c %0t a[1]=%0d", $time, a[1]);
  always @(r[1]) if ($time != 0) $display("#c %0t r[1]=%0.2f", $time, r[1]);
  initial begin
    #10 x = 5;
    #2 x2 = 5;
    #2 p = 3.0;
    #6 y = 7;
    #2 y2 = 7;
    #2 q = 5.0;
    #6 x = 6;
    #2 x2 = 6;
    #2 p = 4.0;
    #6 $finish;
  end
endmodule
