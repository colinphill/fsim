// SPDX-License-Identifier: Apache-2.0
// fsim: top=sv_disable_called_task_activations
//
// Disabling a named block ends the tasks it called (IEEE 1800-2023 9.6.2):
// "all activities enabled within the named block ... shall be terminated as
// well". A thread suspended in a task the block called, however deep, resumes
// after the block; a task called from the block may disable it; automatic
// frames of the ended calls restore their caller's variables; fork children
// started inside the called task end with it.
module sv_disable_called_task_activations;
  int errors = 0;
  int reached = 0;

  // 1. A wait inside a task called from the block, disabled by another process.
  task automatic slow(input int d);
    int k;
    k = d;
    #10;
    reached = reached + 1;
    $display("FAIL: slow resumed after its block was disabled (k=%0d)", k);
  endtask

  // 2. Nested calls.
  task automatic inner_wait();
    #10;
    reached = reached + 1;
    $display("FAIL: inner_wait resumed after the block was disabled");
  endtask
  task automatic outer_call();
    inner_wait();
    reached = reached + 1;
    $display("FAIL: outer_call continued after the block was disabled");
  endtask

  // 3. A task called from the block disables it.
  int n = 0;
  task check();
    n = n + 1;
    if (n == 3) disable lp;
    if (n > 3) $display("FAIL: check ran after its block was disabled");
  endtask

  // 4. Recursion: the outermost call inside the block ends every activation
  // it started, and the caller's automatic variable is its own again.
  int seen_k = -1;
  int rec_after = 0;
  task automatic rec(input int depth);
    int k;
    k = depth;
    if (depth > 0) begin : rb
      rec(depth - 1);
      rec_after = rec_after + 1;
    end else begin
      #10;
      rec_after = rec_after + 1;
    end
    if (depth == 2) seen_k = k;
  endtask

  // 5. A fork child started inside the called task ends with the block.
  int ticks = 0;
  task automatic spawner();
    fork
      forever #1 ticks = ticks + 1;
    join_none
    #100;
  endtask

  bit after1, after2, after5;
  initial begin
    begin : b1
      slow(1);
      $display("FAIL: b1 continued after slow");
    end
    after1 = 1;
  end
  initial #5 disable b1;

  initial begin
    begin : b2
      outer_call();
      $display("FAIL: b2 continued after outer_call");
    end
    after2 = 1;
  end
  initial #5 disable b2;

  initial begin
    begin : lp
      forever begin
        #1 check();
      end
    end
    if (n != 3) begin
      $display("FAIL: lp ended with n=%0d, want 3", n);
      errors = errors + 1;
    end
  end

  initial rec(2);
  initial #5 disable rec.rb;

  int ticks_at_disable;
  initial begin
    begin : b5
      spawner();
    end
    after5 = 1;
  end
  initial begin
    #5 disable b5;
    ticks_at_disable = ticks;
  end

  initial begin
    #50;
    if (!after1 || !after2 || !after5) begin
      $display("FAIL: a disabled block did not resume after it (%0b%0b%0b)",
        after1, after2, after5);
      errors = errors + 1;
    end
    if (reached != 0) errors = errors + 1;
    if (seen_k != 2) begin
      $display("FAIL: rec(2) saw k=%0d after its block, want 2", seen_k);
      errors = errors + 1;
    end
    if (rec_after != 0) begin
      $display("FAIL: %0d ended rec activations continued", rec_after);
      errors = errors + 1;
    end
    if (ticks != ticks_at_disable || ticks == 0) begin
      $display("FAIL: the fork child ticked %0d times, %0d at the disable",
        ticks, ticks_at_disable);
      errors = errors + 1;
    end
    if (errors == 0) $display("PASS");
    $finish;
  end
endmodule
