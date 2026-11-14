// SPDX-License-Identifier: Apache-2.0
// an unpacked-array nettype and a two-state nettype declared in a package
package p;
function automatic int imax(input int d[]);
  imax = -1;
  foreach (d[i]) if (d[i] > imax) imax = d[i];
endfunction
nettype int wmax with imax;
endpackage
module top;
  import p::*;
  wmax m;
  assign m = 7;
  assign m = 3;
  initial #1 $display("m=%0d", m);
endmodule
