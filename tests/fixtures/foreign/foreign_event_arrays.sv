// SPDX-License-Identifier: Apache-2.0
// Arrays of events (IEEE 1800-2023 6.17, 7.4, 15.5): static, class-property
// and automatic task-local arrays; `->`, `->>`, `@` and `wait(ea[i].triggered)`
// with a run-time index; an element assigned another event (15.5.5); an
// automatic array has its own events in every activation (6.21).
class Holder;
  event pe[2];
  task fire(int i);
    -> pe[i];
  endtask
endclass

module top;
  event sea[3];

  task automatic wait_one(input int n);
    event ea[3];
    int got;
    fork
      begin @(ea[n]); got = 1; $display("#c task ea[%0d] seen at %0d", n, $time); end
    join_none
    #1;
    ->> ea[n];
    #1;
    $display("#c task got=%0d", got);
  endtask

  task automatic level();
    event ea[2];
    fork
      begin wait(ea[1].triggered); $display("#c level seen at %0d", $time); end
    join_none
    #1;
    -> ea[1];
    #1;
  endtask

  task automatic alias_test();
    event ea[2];
    ea[1] = ea[0];
    fork
      begin @(ea[1]); $display("#c alias seen at %0d", $time); end
    join_none
    #1;
    -> ea[0];
    #1;
  endtask

  // Two activations that are in the task together keep their own events.
  task automatic own(input int tag);
    event ea[1];
    fork
      begin @(ea[0]); $display("#c own %0d seen at %0d", tag, $time); end
    join_none
    #2;
    if (tag == 2) -> ea[0];
  endtask

  // An array of a typedef'd event type is an array of events (6.17, 6.18).
  typedef event ev_t;

  task automatic typed();
    ev_t ea[2];
    fork
      begin @(ea[1]); $display("#c typedef seen at %0d", $time); end
    join_none
    #1;
    -> ea[1];
    #1;
  endtask

  task automatic typed_level();
    ev_t ea[2];
    fork
      begin wait(ea[0].triggered); $display("#c typedef level seen at %0d", $time); end
    join_none
    #1;
    -> ea[0];
    #1;
  endtask

  function automatic void quiet(input int n);
    event fe[2];
    -> fe[n];
  endfunction

  initial begin
    Holder h = new;
    fork begin @(h.pe[1]); $display("#c class pe1 at %0d", $time); end join_none
    #1 h.fire(1);
    #1;
    fork begin @(sea[2]); $display("#c static sea2 at %0d", $time); end join_none
    #1 -> sea[2];
    wait_one(1);
    level();
    alias_test();
    fork own(1); own(2); join
    typed();
    typed_level();
    quiet(1);
    #1;
    $display("#c end at %0d", $time);
  end
endmodule
