// fsim: top=sv_automatic_fork_typed_context_controls_batch59 std=2017
// SPDX-License-Identifier: Apache-2.0
module sv_automatic_fork_typed_context_controls_batch59;
  event ordered_first;
  event ordered_second;
  event reversed_first;
  event reversed_second;

  string string_join_any_value;
  string string_late_before;
  string string_late_after;
  int queue_join_any_size;
  int queue_join_any_first;
  int queue_join_any_second;
  int queue_late_before_size;
  int queue_late_before_first;
  int queue_late_before_second;
  int queue_late_before_third;
  int queue_late_after_size;
  int queue_late_after_last;
  int wait_order_success_join_any;
  int wait_order_success_late_before;
  int wait_order_success_late_after;
  int wait_order_failure_join_any;
  int wait_order_failure_late_before;
  int wait_order_failure_late_after;
  string no_fork_string;
  int no_fork_queue_size;
  int no_fork_queue_first;
  int no_fork_queue_second;

  task automatic string_fork_context;
    string local_text;
    local_text = "seed";
    fork
      begin
        #1;
        local_text = {local_text, ":early"};
      end
      begin
        #3;
        string_late_before = local_text;
        local_text = {local_text, ":late"};
        string_late_after = local_text;
      end
    join_any
    string_join_any_value = local_text;
    local_text = {local_text, ":owner"};
  endtask

  task automatic queue_fork_context;
    int local_values[$];
    local_values.delete();
    local_values.push_back(7);
    fork
      begin
        #1;
        local_values.push_back(8);
      end
      begin
        #3;
        queue_late_before_size = local_values.size();
        queue_late_before_first = local_values[0];
        queue_late_before_second = local_values[1];
        queue_late_before_third = local_values[2];
        local_values.push_back(9);
        queue_late_after_size = local_values.size();
        queue_late_after_last = local_values[3];
      end
    join_any
    queue_join_any_size = local_values.size();
    queue_join_any_first = local_values[0];
    queue_join_any_second = local_values[1];
    local_values.push_back(10);
  endtask

  task automatic wait_order_success_context;
    int local_result;
    local_result = 0;
    fork
      begin
        wait_order(ordered_first, ordered_second) begin
          local_result = local_result + 1;
        end else begin
          local_result = local_result + 10;
        end
      end
      begin
        #1;
        -> ordered_first;
        #1;
        -> ordered_second;
        #1;
      end
      begin
        #4;
        wait_order_success_late_before = local_result;
        local_result = local_result + 100;
        wait_order_success_late_after = local_result;
      end
    join_any
    wait_order_success_join_any = local_result;
    local_result = local_result + 10;
  endtask

  task automatic wait_order_failure_context;
    int local_result;
    local_result = 0;
    fork
      begin
        wait_order(reversed_first, reversed_second) begin
          local_result = local_result + 1;
        end else begin
          local_result = local_result + 10;
        end
      end
      begin
        #1;
        -> reversed_second;
        #1;
        -> reversed_first;
        #1;
      end
      begin
        #4;
        wait_order_failure_late_before = local_result;
        local_result = local_result + 100;
        wait_order_failure_late_after = local_result;
      end
    join_any
    wait_order_failure_join_any = local_result;
    local_result = local_result + 10;
  endtask

  task automatic no_fork_typed_control;
    string local_text;
    int local_values[$];
    local_text = "plain";
    local_values.delete();
    local_values.push_back(3);
    local_values.push_back(4);
    no_fork_string = local_text;
    no_fork_queue_size = local_values.size();
    no_fork_queue_first = local_values[0];
    no_fork_queue_second = local_values[1];
  endtask

  initial begin
    string_fork_context();
  end

  initial begin
    queue_fork_context();
  end

  initial begin
    wait_order_success_context();
  end

  initial begin
    wait_order_failure_context();
  end

  initial begin
    no_fork_typed_control();
  end

  initial begin
    #5;
    if (string_join_any_value != "seed:early"
        || string_late_before != "seed:early:owner"
        || string_late_after != "seed:early:owner:late")
      $fatal(1, "automatic string context lost caller or late-sibling updates");
    if (queue_join_any_size !== 2 || queue_join_any_first !== 7
        || queue_join_any_second !== 8 || queue_late_before_size !== 3
        || queue_late_before_first !== 7 || queue_late_before_second !== 8
        || queue_late_before_third !== 10 || queue_late_after_size !== 4
        || queue_late_after_last !== 9)
      $fatal(1, "automatic queue context lost caller or late-sibling updates");
    if (wait_order_success_join_any !== 1
        || wait_order_success_late_before !== 11
        || wait_order_success_late_after !== 111)
      $fatal(1, "suspended shared context lost successful wait_order result");
    if (wait_order_failure_join_any !== 10
        || wait_order_failure_late_before !== 20
        || wait_order_failure_late_after !== 120)
      $fatal(1, "suspended shared context lost failed wait_order result");
    if (no_fork_string != "plain" || no_fork_queue_size !== 2
        || no_fork_queue_first !== 3 || no_fork_queue_second !== 4)
      $fatal(1, "no-fork automatic string or queue control failed");
    $display("PASS");
  end
endmodule
