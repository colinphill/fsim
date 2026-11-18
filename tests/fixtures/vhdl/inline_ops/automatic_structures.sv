// SPDX-License-Identifier: Apache-2.0
//
// Unpacked structures of an automatic task, a structure value and a fixed
// array of them, held by the v4 kernel as flat leaves (IEEE 1800-2023 7.2,
// 7.4): member writes and reads through nested structures, a two-state,
// four-state and real leaf each, a run-time element index, a whole copy.
//   s = {5, 4'h3, {6, 2.5}}; arr[2].a = 7, arr[1].b = 9, arr[3].n.c = 5;
//   arr[2].b was never written: logic reads x; arr[1].n.c is a bit: 0.
//   arr[7] is out of range: the int member reads 0, the logic one x (7.4.6).
//   copy = s, then s.a = 9: the copy keeps 5.
module top;
  typedef struct { int a; logic [3:0] b; struct { bit [2:0] c; real r; } n; } s_t;
  task automatic run(int k);
    s_t s;
    s_t arr [3:1];
    s_t copy;
    s.a = 5;
    s.b = 4'h3;
    s.n.c = 3'd6;
    s.n.r = 2.5;
    arr[k].a = 7;
    arr[1].b = 4'h9;
    arr[3].n.c = 3'd5;
    $display("inl box %0d %h %0d %f", s.a, s.b, s.n.c, s.n.r);
    $display("inl arr %0d %h %0d %h %0d %0d", arr[k].a, arr[1].b, arr[2].a, arr[2].b, arr[3].n.c, arr[1].n.c);
    $display("inl out %0d %h", arr[k + 5].a, arr[k + 5].b);
    copy = s;
    s.a = 9;
    $display("inl copy %0d %0d %h", copy.a, s.a, copy.b);
  endtask
  initial run(2);
endmodule
