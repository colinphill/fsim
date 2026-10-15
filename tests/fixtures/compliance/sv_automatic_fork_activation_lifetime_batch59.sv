// fsim: top=sv_automatic_fork_activation_lifetime_batch59 std=2017
// SPDX-License-Identifier: Apache-2.0
module sv_automatic_fork_activation_lifetime_batch59;
  integer join_any_after = 0;
  integer join_any_late = 0;
  integer join_any_time_result = 0;
  integer join_all_after = 0;
  integer join_none_late = 0;
  integer recursive_result = 0;
  integer recursive_inner_result = 0;
  integer no_fork_four = 0;
  integer no_fork_five = 0;

  task automatic join_any_shared_activation;
    integer local_value;
    integer time_result;
    local_value = 0;
    fork
      begin
        #1;
        local_value = local_value + 1;
      end
      begin
        #3;
        local_value = local_value + 1;
        join_any_late = local_value;
      end
    join_any
    join_any_after = local_value;
    time_result = 0;
    time_result = $time;
    join_any_time_result = time_result;
    local_value = local_value + 10;
  endtask

  task automatic join_all_nested_join_none;
    integer local_value;
    local_value = 0;
    fork
      begin
        fork
          begin
            #3;
            local_value = local_value + 1;
            join_none_late = local_value;
          end
        join_none
      end
      begin
        #1;
        local_value = local_value + 1;
      end
    join
    join_all_after = local_value;
    local_value = local_value + 10;
  endtask

  task automatic recursive_activation(input integer depth);
    integer local_value;
    local_value = depth;
    if (depth == 2) begin
      fork
        begin
          #1;
          local_value = local_value + 100;
        end
        begin
          recursive_activation(1);
          recursive_result = local_value;
        end
      join
    end else begin
      #2;
      local_value = $time;
      local_value = local_value + 10;
      recursive_inner_result = local_value;
    end
  endtask

  task automatic no_fork_leaf(input integer value);
    integer local_value;
    local_value = value;
    local_value = local_value + 1;
    if (value == 4)
      no_fork_four = local_value;
    else if (value == 5)
      no_fork_five = local_value;
  endtask

  task automatic no_fork_outer(input integer value);
    integer local_value;
    local_value = value;
    no_fork_leaf(local_value);
  endtask

  initial begin
    join_any_shared_activation();
  end

  initial begin
    join_all_nested_join_none();
  end

  initial begin
    recursive_activation(2);
  end

  initial begin
    no_fork_outer(4);
    no_fork_outer(5);
  end

  initial begin
    #5;
    if (join_any_after !== 1 || join_any_late !== 12)
      $fatal(1, "join_any activation did not share caller and sibling updates");
    if (join_any_time_result !== 1)
      $fatal(1, "active shared automatic register write was not visible");
    if (join_all_after !== 1 || join_none_late !== 12)
      $fatal(1, "join_all activation did not retain its nested join_none child");
    if (recursive_result !== 102 || recursive_inner_result !== 12)
      $fatal(1, "recursive automatic activation did not restore its caller");
    if (no_fork_four !== 5 || no_fork_five !== 6)
      $fatal(1, "no-fork nested automatic call control failed");
    $display("PASS");
    $finish;
  end
endmodule
