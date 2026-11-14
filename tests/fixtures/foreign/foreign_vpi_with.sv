// SPDX-License-Identifier: Apache-2.0
// The with expression of an array locator method call (IEEE 1800-2023 7.12.1,
// 37.42 detail 1): vpiWith of q.find is its with expression; q.sum is no
// locator method and q.min has no with clause, so they have none.
module top;
  int q[$] = '{1, 5, 9};
  int r[$];
  int m;
  initial begin
    r = q.find with (item > 4);
    m = q.sum with (item * 2);
    r = q.min;
  end
endmodule
