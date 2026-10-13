// fsim: top=sv_macro_continued_comments_batch56 std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 22.5.1: continued lines remain part of macro text.
`define COMMENT_SUM(a,b,c) \
  ((a) + /* comment across \
   two physical lines */ (b)) /* another \
\
   continued comment */ * (c)
`define NEXT_VALUE 17
module sv_macro_continued_comments_batch56;
  initial begin
    if (`COMMENT_SUM(1,2,3) != 9 || `NEXT_VALUE != 17)
      $fatal(1, "comment line continuation lost macro tokens");
    $display("PASS");
  end
endmodule
