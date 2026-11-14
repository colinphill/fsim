// SPDX-License-Identifier: Apache-2.0
// `--save-at` and `--restart` (IEEE 1800-2023 D.9) with a live object of a
// specialization bound to a pooled constant: C #(M) with M = P * 2 + P * 2
// is C#(4) (8.25, 6.22.1), though no literal 4 is in the design. The saved
// object keeps its class after a restart in a fresh process, and the handles
// of C #(M) and C #(2 + 2) still are one type there. Expected output, derived
// by hand: the object `a` is made at t=0 and holds 11; at t=5 both lines
// agree; the save is at t=50; at t=60 the restart reads the object again
// (save_restart_pool_class.restart.expected has the t=60 line).
`timescale 1ns/1ns
class C #(int W = 6);
  int v;
  function int width(); return W; endfunction
endclass

module sub #(parameter int P = 1);
  localparam int M = P * 2 + P * 2;
  C #(M) a;
  C #(2 + 2) b;
  initial begin
    a = new;
    a.v = 11;
    #5 $display("t=%0d w=%0d v=%0d", $time, a.width(), a.v);
    #55 b = a;
    $display("t=%0d w=%0d v=%0d same=%0d", $time, b.width(), b.v,
        $typename(a) == $typename(b));
  end
endmodule

module top;
  sub s();
endmodule
