// SPDX-License-Identifier: Apache-2.0
// Values in module processes and automatic tasks: $cast to an enumeration
// takes the source's own value, so a value that names a member only after
// truncation is no member and the destination keeps its value (IEEE
// 1800-2023 6.24.2); a block-local real or integral variable's initializer
// converts as an assignment does, rounding away from zero (6.8, 6.12.2);
// the sampled value of an automatic variable, and a past value of one, is
// its current value, so it never changes, rises or falls (16.5.1, 16.9.3).
typedef enum logic [1:0] {RED, GREEN, BLUE} color_t;
module top;
  color_t color = BLUE;
  int k;
  task automatic locals();
    real lr = 3;
    int li = 7.6;
    shortint ls = -2.5;
    begin
      real br = 5;
      int bi = -7.5;
      $display("task %f %0d %0d %f %0d", lr, li, ls, br, bi);
    end
  endtask
  task automatic samples(input int a);
    int n = a + 1;
    $display("sampled %0d %0d %0d", $sampled(n), $sampled(n * 2 + a), $past(n));
    $display("changes %b %b %b %b", $stable(n), $changed(n), $rose(n[0]), $fell(n[0]));
  endtask
  initial begin
    k = $cast(color, 6);
    $display("cast %0d %s", k, color.name());
    k = $cast(color, 1);
    $display("cast %0d %s", k, color.name());
    k = $cast(color, -2);
    $display("cast %0d %s", k, color.name());
    begin
      real ir = 2;
      int ii = 2.5;
      $display("block %f %0d", ir, ii);
    end
    locals();
    samples(4);
  end
endmodule
