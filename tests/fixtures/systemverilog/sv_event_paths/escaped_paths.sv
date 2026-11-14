// SPDX-License-Identifier: Apache-2.0
// Hierarchical names through escaped identifiers that contain '.' and '['
// (IEEE 1800-2023 5.6.1, 23.6, 23.8): the escaped name is one scope, so a
// path is split only at the separators outside it.
module leaf #(parameter string LABEL = "");
  int x;
  event ev;
  initial begin
    @(ev);
    $display("sep %s ev %0t", LABEL, $time);
  end
endmodule

module mid;
  int q = 3;
  inner l2 ();
endmodule

module inner;
  // An upward name that starts with the module name of an ancestor (23.8).
  initial #4 $display("sep w.b.l2 mid.q=%0d", mid.q);
endmodule

module top;
  leaf #("u.a") \u.a ();
  leaf #("v[1]") \v[1] ();
  mid \w.b ();
  initial begin
    \u.a .x = 5;
    \v[1] .x = 6;
    #1 $display("sep u.a.x=%0d v[1].x=%0d", \u.a .x, \v[1] .x);
    #1 -> \u.a .ev;
    #1 -> top.\v[1] .ev;
  end
endmodule
