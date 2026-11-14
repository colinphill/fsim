// SPDX-License-Identifier: Apache-2.0
// unpacked array nettype and 2-state int, port merge, force/release
typedef real TR[3];
function automatic TR trsum(input TR d[]);
  foreach (trsum[k]) trsum[k] = 0.0;
  foreach (d[i]) foreach (trsum[k]) trsum[k] += d[i][k];
endfunction
nettype TR wTR with trsum;
function automatic int imax(input int d[]);
  imax = -1;
  foreach (d[i]) if (d[i] > imax) imax = d[i];
endfunction
nettype int wmax with imax;
module leaf(output wTR o, output wmax m);
  assign o = '{1.0, 2.0, 3.0};
  assign m = 7;
endmodule
module top;
  wTR t;
  wmax m;
  leaf l(.o(t), .m(m));
  assign t = '{10.0, 20.0, 30.0};
  assign m = 3;
  initial begin
    #1 $display("t=%f %f %f m=%0d", t[0], t[1], t[2], m);
    force m = 100; #1 $display("forced m=%0d", m);
    release m; #1 $display("released m=%0d", m);
  end
endmodule
