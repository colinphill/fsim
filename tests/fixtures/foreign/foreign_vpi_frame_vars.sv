// SPDX-License-Identifier: Apache-2.0
// The automatic variables of a frame (IEEE 1800-2023 37.43 vpiAutomatics) and
// cbStmt on the statements of a task another instance holds, called by a
// hierarchical name (38.36.1.1).
module leaf;
  integer count = 0;
  task automatic bump(input integer step);
    integer twice;
    begin
      twice = step * 2;
      count = count + twice;
    end
  endtask
  task automatic describe(input string who, input int n);
    string tag;
    int mark;
    int pending[$];
    begin
      tag = {who, "!"};
      mark = n;
      pending.push_back(n);
      $display("#c describe %s %0d %0d", tag, mark, pending.size());
    end
  endtask
  task tally(input int n);
    int total;
    automatic int tmp;
    begin
      tmp = n;
      total = total + tmp;
    end
  endtask
endmodule
module top;
  leaf a();
  leaf b();
  initial begin
    a.bump(1);
    b.bump(2);
    a.describe("x", 7);
    a.tally(3);
    #1 $display("#c done a=%0d b=%0d", a.count, b.count);
  end
endmodule
