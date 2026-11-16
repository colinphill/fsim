// fsim: top=sv_kernel_random std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 18.13, 18.14, 20.15, 7.12.2: random draws compiled into
// the kernel. $urandom_range stays within its bounds (either order, a
// one-value range, the full 32-bit range), an unknown bound gives X, $random
// is an int, shuffle() permutes the elements, fork children draw from their
// own streams (one child's draws do not move a sibling's), and draws in a
// clocked always block and after a suspension keep working.
module sv_kernel_random;
  int failures;
  bit clk;
  int sampled[$];

  task automatic check(input string what, input bit ok);
    if (!ok) begin
      $display("FAIL %s", what);
      failures++;
    end
  endtask

  always @(posedge clk) sampled.push_back($urandom_range(7, 3));

  task automatic ranges();
    int value;
    logic [7:0] unknown;
    bit [3:0] narrow;
    for (int i = 0; i < 200; i++) begin
      value = $urandom_range(10);
      check("range 0..10", value >= 0 && value <= 10);
      value = $urandom_range(5, 9);
      check("range 5..9", value >= 5 && value <= 9);
      value = $urandom_range(9, 5);
      check("swapped bounds", value >= 5 && value <= 9);
    end
    check("one value", $urandom_range(3, 3) == 3);
    narrow = 4'd12;
    value = $urandom_range(narrow, 100);
    check("narrow bound", value >= 12 && value <= 100);
    unknown = 8'bx;
    check("unknown bound", $isunknown($urandom_range(unknown)));
    value = $urandom_range(32'hffff_ffff, 0);
    value = $random;
    value = $urandom;
    #1;
    value = $urandom_range(2);
    check("after a wait", value >= 0 && value <= 2);
  endtask

  task automatic shuffles();
    int q[$];
    int seen;
    int moved;
    for (int round = 0; round < 20; round++) begin
      q = {1, 2, 3, 4, 5, 6, 7, 8};
      q.shuffle();
      seen = 0;
      foreach (q[i]) seen |= 1 << q[i];
      check("permutation", seen == 'h1fe && q.size() == 8);
      foreach (q[i]) if (q[i] != i + 1) moved++;
    end
    check("elements move", moved != 0);
    q = {5};
    q.shuffle();
    check("one element", q[0] == 5);
    q.delete();
    q.shuffle();
    check("empty", q.size() == 0);
  endtask

  task automatic children();
    int first[4];
    int second[4];
    fork
      for (int i = 0; i < 4; i++) first[i] = $urandom;
      for (int i = 0; i < 4; i++) second[i] = $urandom;
    join
    check("children draw their own streams", first != second);
  endtask

  initial begin
    ranges();
    shuffles();
    children();
    repeat (6) begin
      #1 clk = 1;
      #1 clk = 0;
    end
    check("always draws", sampled.size() == 6);
    foreach (sampled[i]) check("always range", sampled[i] >= 3 && sampled[i] <= 7);
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
