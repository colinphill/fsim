// SPDX-License-Identifier: Apache-2.0
//
// `always @(cb)` waits for the clocking event of clocking block cb (IEEE
// 1800-2023 14.13), as a statement's `@(cb)` does: here posedge clk, which
// occurs at 5, 15, ..., 95 before time 100 -- ten times, not at both edges
// of clk. The guarded form `@(cb iff en)` counts only the events while en
// is 1 (9.4.2.3): those at 45 to 95.
module top;
  logic clk = 1'b0;
  logic en = 1'b0;
  int n;
  int m;
  int g;
  int first = -1;
  always #5 clk = ~clk;
  clocking cb @(posedge clk);
  endclocking
  always @(cb) n++;
  always @(cb) begin
    if (first < 0) first = $time;
    m++;
  end
  always @(cb iff en) g++;
  initial begin
    #40 en = 1'b1;
    #60 $display("inl n=%0d m=%0d g=%0d first=%0d", n, m, g, first);
    $finish;
  end
endmodule
