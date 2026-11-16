// fsim: top=sv_for_assigns_loop_variable std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 12.7.1: the variable a for statement declares is an
// ordinary variable of the loop, so the body may assign it; the next test of
// the condition sees the assigned value. Covered forms: a blocking
// assignment, an operator assignment and an increment statement, one in a
// nested block, one in a case item, a loop with a delay and two variables,
// and a loop in an automatic function.
`define CHECK(what, ok) \
  if (!(ok)) begin \
    $display("FAIL %s", what); \
    failures++; \
  end
module sv_for_assigns_loop_variable;
  int failures;

  function automatic int skipping(int limit);
    int count = 0;
    for (int i = 0; i < limit; i++) begin
      count++;
      case (i)
        1: i = i + 2;
        default: ;
      endcase
    end
    return count;
  endfunction

  initial begin
    int jumped, stepped, nested, timed;
    for (int i = 0; i < 10; i++) begin
      jumped++;
      if (i == 2) i = 7;
    end
    `CHECK("blocking assignment", jumped == 5)
    for (int i = 0; i < 8; i++) begin
      stepped++;
      i++;
    end
    `CHECK("increment statement", stepped == 4)
    for (int i = 0; i < 6; i++) begin
      begin
        automatic int seen = i;
        if (seen == 1) i += 3;
      end
      nested++;
    end
    `CHECK("operator assignment in a nested block", nested == 3)
    for (int i = 0, j = 10; i < j; i++, j--) begin
      #1 timed++;
      if (timed == 2) j = 3;
    end
    `CHECK("second variable with a delay", timed == 2 && $time == 2)
    `CHECK("case item in a function", skipping(6) == 4)
    if (failures == 0) begin
      $display("PASS");
    end
    $finish;
  end
endmodule
