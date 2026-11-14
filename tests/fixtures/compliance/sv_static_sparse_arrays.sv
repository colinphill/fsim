// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 7.4, 8.9, 8.25: a fixed-size unpacked array has every
// element its declaration gives it, whatever its size. A static property of
// a class, of each specialization of a parameterized class, a package
// variable and a module variable here have 16M elements each; an element
// never written reads as its type's default (6.8 Table 6-7: 0 for int, X
// for logic), and the written ones keep their values.
package p;
  int big[16777216];
  logic [7:0] wide[0:16777215];
endpackage

module top;
  class K;
    static int big[16777216];
    static function void put(int index, int value);
      big[index] = value;
    endfunction
  endclass

  class Q #(int W = 8);
    static logic [W-1:0] big[16777216];
  endclass

  int big[16777216];
  int failures;
  int sum;

  initial begin
    K::big[5] = 7;
    K::put(16777215, 9);
    K::big[3] += 2;
    Q#(8)::big[1] = 8'hA5;
    Q#(4)::big[1] = 4'h3;
    p::big[16777215] = 11;
    p::wide[0] = 8'h5A;
    big[16777215] = 13;
    big[0] = big[16777215] + 1;
    for (int i = 0; i < 4; i++) sum += K::big[i] + K::big[i + 4];
    if (K::big[5] != 7 || K::big[16777215] != 9 || K::big[3] != 2
        || K::big[100] != 0 || sum != 9)
      failures++;
    if (Q#(8)::big[1] !== 8'hA5 || Q#(4)::big[1] !== 4'h3
        || Q#(8)::big[2] !== 8'hxx || Q#(4)::big[16777215] !== 4'hx)
      failures++;
    if (p::big[16777215] != 11 || p::big[1] != 0 || p::wide[0] !== 8'h5A
        || p::wide[1] !== 8'hxx)
      failures++;
    if (big[16777215] != 13 || big[0] != 14 || big[12345] != 0)
      failures++;
    if ($size(K::big) != 16777216 || $size(big) != 16777216)
      failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL count %0d", failures);
  end
endmodule
