// SPDX-License-Identifier: Apache-2.0
//
// Fixed-size unpacked arrays of strings of an automatic task, which the v4
// kernel holds as its own containers (the dynamic array layout, a string
// element in each text slot, elements in declared order, left first) and
// runs in compiled member code:
//   6.16: a string variable starts as the empty string, so up[2] reads "";
//   7.4: an element is addressed by its declared index: s[0:2], up[1:3]
//   and down[2:0];
//   12.7.3: foreach walks down[2:0] from its left bound: 2, 1, 0;
//   7.6, 11.4.5: assignment and equality of the whole arrays go element by
//   element; copied = s, then a change of copied[1] leaves s[1];
//   21.2.1.6: %p prints each array as an assignment pattern of quoted
//   strings, left element first.
// s[1] = {s[0], s[2]} = "abxyz" (11.4.12.2), of length 5 (6.16.1).
module top;
  task automatic run();
    string s[3];
    string up[1:3];
    string down[2:0];
    string copied[3];
    string word;
    int i;
    s[0] = "ab";
    s[2] = "xyz";
    s[1] = {s[0], s[2]};
    $display("inl s [%s] [%s] [%s] %0d", s[0], s[1], s[2], s[1].len());
    up[1] = "one";
    up[3] = "three";
    $display("inl up [%s] [%s] [%s]", up[1], up[2], up[3]);
    down[0] = "d0";
    down[2] = "d2";
    down[1] = "d1";
    foreach (down[k]) $display("inl down %0d=%s", k, down[k]);
    copied = s;
    $display("inl equal %0d %0d", copied == s, copied != s);
    copied[1] = "q";
    $display("inl changed %0d [%s] [%s]", copied == s, copied[1], s[1]);
    i = 2;
    word = s[i];
    $display("inl dynamic [%s]", word);
    $display("inl pattern %p %p", up, down);
  endtask
  initial run();
endmodule
