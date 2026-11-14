// SPDX-License-Identifier: Apache-2.0
// $unit nettype, no ports
function automatic int imax(input int d[]);
  imax = -1;
  foreach (d[i]) if (d[i] > imax) imax = d[i];
endfunction
nettype int wmax with imax;
module top;
  wmax m;
  assign m = 7;
  assign m = 3;
  initial #1 $display("m=%0d", m);
endmodule
