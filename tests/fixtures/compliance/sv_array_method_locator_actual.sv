// fsim: top=sv_array_method_locator_actual std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 7.12.1, 13.5: a locator method returns a queue, which is
// passed as an actual argument for a queue formal.
module sv_array_method_locator_actual;
  int failures;

  task automatic check_queue(input string what, input int x[$], input int n, input int first);
    if (x.size() != n || (n > 0 && x[0] != first)) begin
      $display("FAIL %s: size %0d first %0d want size %0d first %0d",
               what, x.size(), n > 0 ? x[0] : -1, n, first);
      failures++;
    end
  endtask

  task automatic run();
    int q[$] = '{5, 3, 9, 6, 1};
    check_queue("min", q.min(), 1, 1);
    check_queue("max", q.max(), 1, 9);
    check_queue("find", q.find with (item > 4), 3, 5);
    check_queue("find_index", q.find_index with (item > 4), 3, 0);
    check_queue("min with", q.min() with (item % 4), 1, 5);
    check_queue("none", q.find with (item > 100), 0, 0);
  endtask

  initial begin
    run();
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
