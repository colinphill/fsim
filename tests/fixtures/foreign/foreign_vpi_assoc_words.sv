// SPDX-License-Identifier: Apache-2.0
// Associative array words through VPI (IEEE 1800-2023 37.3.8, 37.17 detail
// 28): a word is named by its index key, and its handle stays valid while
// the key exists, across inserts and deletes of other keys.
module top;
  int aa[int];
  int sa[string];
  initial begin
    // t=2: the first keys.
    #2 aa[5] = 50;
    aa[9] = 90;
    sa["a.b"] = 1;
    sa["q\"t"] = 2;
    // t=4: a key below 5 moves word aa[5] to the second position.
    #2 aa[2] = 20;
    // t=6: only key 9 goes away.
    #2 aa.delete(9);
    // t=8: the kept word changes value.
    #2 aa[5] = 55;
    #2 $finish;
  end
endmodule
