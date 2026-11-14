// SPDX-License-Identifier: Apache-2.0
// class static method as the resolution function (6.6.7)
class R;
  static function int sum(input int d[]);
    sum = 0;
    foreach (d[i]) sum += d[i];
  endfunction
endclass
module top;
  nettype int nt with R::sum;
  nt w;
  int a = 3, b = 4;
  assign w = a;
  assign w = b;
  initial begin #1 $display("w=%0d", w); a = 30; #1 $display("w=%0d", w); end
endmodule
