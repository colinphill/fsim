// SPDX-License-Identifier: Apache-2.0
// A frame's automatic event array and its elements (IEEE 1800-2023 37.43
// detail 1; an element is a named event that vpi_put_value triggers, 38.34),
// and the call that activated a frame (37.43 detail 6): found from the call
// the frame's call point reports, so two calls of one function in one
// expression, and two calls of one task, resolve each to its own call.
module top;
  int sink;
  event sea[3];

  task automatic ev_arr(input int n);
    event ea[2];
    begin
      fork
        begin @(ea[1]); $display("#c element seen at %0d", $time); end
      join_none
      #2;
      $display("#c ev_arr end at %0d", $time);
    end
  endtask

  function automatic int f(input int a);
    int r;
    begin
      r = a;
      return r;
    end
  endfunction

  task automatic work(input int id);
    begin
      #1;
    end
  endtask

  int k1 = 1;
  int k2 = 2;

  initial begin
    fork
      begin @(sea[2]); $display("#c module element seen at %0d", $time); end
    join_none
    ev_arr(1);
    sink = f(k1) + f(k2);
    work(10);
    work(20);
  end
  typedef event ev_t;

  // An event declared through a typedef, and an array of them, are named
  // events of the frame (6.18, 37.43); a put to an element no thread has used
  // yet triggers it for the rest of the time step (15.5.3, 38.34), so the
  // wait does not block.
  task automatic ev_typed();
    ev_t te;
    ev_t ta[2];
    begin
      #1;
      wait(ta[1].triggered);
      $display("#c typed triggered seen at %0d", $time);
    end
  endtask

  initial #20 ev_typed();
endmodule
