// SPDX-License-Identifier: Apache-2.0
//
// A reduction over a two-dimensional array of an automatic task whose with
// clause reduces each row (IEEE 1800-2023 7.12.3): the row copies index
// their elements through checked integer arithmetic, which the compiled
// member runs as generated code.
//   m[r][c] = 16*r + c: rows 0 1 2 and 16 17 18; row sums 3 and 51;
//   m.sum() with (item.sum()) = 54; m[1].sum() = 51.
// The result has the with expression's type, int (signed): over w with
// rows {32'h7fffffff, 1} and {2, 3} the first row's sum wraps to
// -2147483648 and the whole sum is -2147483643.
module top;
  task automatic run();
    int m[1:0][0:2];
    int w[1:0][0:1];
    for (int r = 0; r < 2; r++)
      for (int c = 0; c < 3; c++) m[r][c] = r * 16 + c;
    $display("inl nested %0d row=%0d", m.sum() with (item.sum()), m[1].sum());
    w[0][0] = 32'h7fffffff;
    w[0][1] = 1;
    w[1][0] = 2;
    w[1][1] = 3;
    $display("inl wrapped %0d", w.sum() with (item.sum()));
  endtask
  initial run();
endmodule
