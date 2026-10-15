// fsim: top=sv_automatic_fork_mailbox_wake_batch59 std=2017
// SPDX-License-Identifier: Apache-2.0
module sv_automatic_fork_mailbox_wake_batch59;
  integer mailbox_owner_after = -1;
  integer mailbox_reader_after = -1;
  integer mailbox_late_before = -1;
  integer mailbox_late_after = -1;
  integer no_fork_mailbox_value = -1;
  mailbox #(int) fork_mailbox;
  mailbox #(int) no_fork_mailbox;

  task automatic mailbox_join_any_context;
    int local_value;
    local_value = -1;
    fork
      begin
        fork_mailbox.get(local_value);
        local_value = local_value + 1;
        mailbox_reader_after = local_value;
      end
      begin
        #1;
        fork_mailbox.put(42);
        #1;
      end
      begin
        #4;
        mailbox_late_before = local_value;
        local_value = local_value + 100;
        mailbox_late_after = local_value;
      end
    join_any
    mailbox_owner_after = local_value;
    local_value = local_value + 10;
  endtask

  task automatic no_fork_mailbox_control;
    int local_value;
    no_fork_mailbox.put(5);
    no_fork_mailbox.get(local_value);
    no_fork_mailbox_value = local_value;
  endtask

  initial begin
    fork_mailbox = new(1);
    mailbox_join_any_context();
  end

  initial begin
    no_fork_mailbox = new(1);
    no_fork_mailbox_control();
  end

  initial begin
    #5;
    if (mailbox_reader_after !== 43 || mailbox_owner_after !== 43)
      $fatal(1, "mailbox wake result did not reach the reader and join_any owner");
    if (mailbox_late_before !== 53 || mailbox_late_after !== 153)
      $fatal(1, "automatic mailbox activation did not retain late sibling updates");
    if (no_fork_mailbox_value !== 5)
      $fatal(1, "no-fork automatic mailbox control failed");
    $display("PASS");
    $finish;
  end
endmodule
