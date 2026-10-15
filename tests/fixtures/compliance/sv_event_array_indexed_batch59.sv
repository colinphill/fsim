// fsim: top=sv_event_array_indexed_batch59 std=2017
// SPDX-License-Identifier: Apache-2.0
module sv_event_array_indexed_batch59;
  event scalar_before, ranged_events[5:4], scalar_after;
  event zero_events[2];

  int wake4 = 0;
  int wake5 = 0;
  int scalar_wakes = 0;
  int descending_static_wakes = 0;
  int zero_wakes = 0;
  logic first_fire = 0;
  logic second_fire = 0;
  logic first_seen;
  logic second_seen;

  sv_event_array_owner first_owner(first_fire, first_seen);
  sv_event_array_owner second_owner(second_fire, second_seen);

  task automatic wait_dynamic(input int index_value);
    @(ranged_events[index_value]);
    if (index_value == 4)
      wake4++;
    else if (index_value == 5)
      wake5++;
  endtask

  task automatic trigger_dynamic(input int index_value);
    -> ranged_events[index_value];
  endtask

  always @(ranged_events[5])
    descending_static_wakes++;
  always @(scalar_before)
    scalar_wakes++;
  always @(scalar_after)
    scalar_wakes++;
  always @(zero_events[0])
    zero_wakes++;

  initial begin
    fork
      wait_dynamic(4);
      wait_dynamic(5);
    join_none

    #1 trigger_dynamic(4);
    #1;
    if (wake4 != 1 || wake5 != 0)
      $fatal(1, "event-array trigger woke the wrong indexed waiter");

    #1 trigger_dynamic(5);
    #1;
    if (wake4 != 1 || wake5 != 1 || descending_static_wakes != 1)
      $fatal(1, "event-array wait did not wake on its selected event");

    -> ranged_events[5];
    -> zero_events[0];
    -> scalar_before;
    -> scalar_after;
    #1;
    if (zero_wakes != 1 || scalar_wakes != 2
        || descending_static_wakes != 2)
      $fatal(1, "static event-array or adjacent scalar event failed");

    #1 first_fire = 1'b1;
    #1;
    if (first_seen !== 1'b1 || second_seen !== 1'b0)
      $fatal(1, "event arrays in separate instances were not independent");
    #1 second_fire = 1'b1;
    #1;
    if (first_seen !== 1'b1 || second_seen !== 1'b1)
      $fatal(1, "the second instance event array did not trigger");

    $display("PASS");
    $finish;
  end
endmodule

module sv_event_array_owner(input logic fire, output logic observed);
  event owner_events[1:0];

  initial observed = 1'b0;
  always @(owner_events[1])
    observed = 1'b1;
  always @(fire)
    if (fire)
      -> owner_events[1];
endmodule
