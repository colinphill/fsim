// fsim: top=sv_process_method_receivers std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 9.7: the methods of the process class apply to any
// expression whose type is process: a variable, a class property, an array
// or queue element, a function or class method result, and an element
// popped from a queue (7.10.2.6). status() returns the `state` enum, whose
// name() gives the constant (6.19.5.6). A process killed by a child forked
// with join_none, while its parent kills another, ends like any other.
`define CHECK(what, ok) \
  if (!(ok)) begin \
    $display("FAIL %s", what); \
    failures++; \
  end
module sv_process_method_receivers;
  class holder;
    process p;
    function process get();
      return p;
    endfunction
  endclass
  int failures;
  int ended[6];
  process saved;
  process procs[$];
  process arr[2];
  process waited;
  holder h;
  function automatic process get_saved();
    return saved;
  endfunction
  initial begin
    h = new;
    fork
      begin saved = process::self(); #100 ended[0] = 1; end
      begin procs.push_back(process::self()); #100 ended[1] = 1; end
      begin h.p = process::self(); #100 ended[2] = 1; end
      begin arr[1] = process::self(); #100 ended[3] = 1; end
      begin waited = process::self(); #5 ended[4] = 1; end
      begin arr[0] = process::self(); #100 ended[5] = 1; end
    join_none
    #1;
    `CHECK("function result status", get_saved().status() == process::WAITING)
    `CHECK("method result status", h.get().status() == process::WAITING)
    `CHECK("property status", h.p.status() == process::WAITING)
    `CHECK("element status", arr[1].status() == process::WAITING)
    `CHECK("status name", get_saved().status().name() == "WAITING")
    `CHECK("method status name", h.get().status().name() == "WAITING")
    get_saved().kill();
    procs.pop_front().kill();
    h.get().kill();
    fork arr[1].kill(); join_none
    arr[0].kill();
    #1;
    `CHECK("function result killed", saved.status() == process::KILLED)
    `CHECK("property killed", h.p.status().name() == "KILLED")
    `CHECK("forked kill", arr[1].status() == process::KILLED)
    `CHECK("parent kill", arr[0].status() == process::KILLED)
    h.p = waited;
    h.get().await();
    `CHECK("await through a method", waited.status() == process::FINISHED)
    #200;
    `CHECK("killed processes did not run on",
      ended[0] == 0 && ended[1] == 0 && ended[2] == 0 && ended[3] == 0
        && ended[5] == 0)
    `CHECK("awaited process ended", ended[4] == 1)
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
