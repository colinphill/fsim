// SPDX-License-Identifier: Apache-2.0
//
// $dist_* and tagged-union tag checks in the v4 kernel's generated code
// (IEEE 1800-2023 20.14.2, 11.9): each call writes its seed variable back,
// so two runs from the same seed draw the same sequence; a member access
// under the current tag reads it, and one under another tag reports the
// mismatch (an error) and goes on.
module top;
  typedef union tagged { int Num; bit [7:0] Byte; } item_t;
  item_t it;
  int seed, s1, s2, n, mismatch;
  int a[4], b[4];
  initial begin
    s1 = 5;
    s2 = 5;
    for (int i = 0; i < 4; i++) a[i] = $dist_uniform(s1, 0, 99);
    for (int i = 0; i < 4; i++) b[i] = $dist_uniform(s2, 0, 99);
    $display("inl uniform same %0d seeds same %0d moved %0d", a == b, s1 == s2, s1 != 5);
    seed = 9;
    n = $dist_normal(seed, 100, 0);
    $display("inl normal %0d", n);
    seed = 9;
    n = $dist_poisson(seed, 0);
    $display("inl poisson %0d", n);
    seed = 3;
    for (int i = 0; i < 4; i++) a[i] = $dist_exponential(seed, 10);
    seed = 3;
    for (int i = 0; i < 4; i++) b[i] = $dist_exponential(seed, 10);
    $display("inl exponential same %0d", a == b);
    it = tagged Num 42;
    $display("inl tag %0d", it.Num);
    it = tagged Byte 8'h5a;
    $display("inl tag %h", it.Byte);
    mismatch = it.Num;
    $display("inl after mismatch");
  end
endmodule
