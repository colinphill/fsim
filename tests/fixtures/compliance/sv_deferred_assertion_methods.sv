// fsim: top=sv_deferred_assertion_methods
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 16.4, 16.4.1: the action of an observed deferred assertion
// may be a task method or a void function method, and its by-value actuals
// (a queue included) are copied when the assertion is evaluated.  At time 0
// v == 5 and items == {7, 8, 9}; both are changed in the same Active region
// before the Reactive region runs the queued calls.  Derived results:
//   add(v, items):  total += 5, calls = 1, last_size = 3, last_first = 7
//   note(v):        total += 5 * 1000
//   total == 5005.
module sv_deferred_assertion_methods;
  timeunit 1ns; timeprecision 1ns;
  class counter;
    int total, calls, last_size, last_first;
    task automatic add(input int v, input int q_in[$]);
      total += v;
      calls++;
      last_size = q_in.size();
      last_first = q_in[0];
    endtask
    function void note(input int v);
      total += v * 1000;
    endfunction
  endclass
  counter c;
  int items[$];
  int v;
  int failures;

  initial begin
    c = new;
    v = 5;
    items = '{7, 8, 9};
    assert #0 (v == 5) c.add(v, items);
    assert #0 (v == 5) c.note(v);
    v = 6;
    items = '{1};
    #1;
    if (c.total != 5005) failures++;
    if (c.calls != 1) failures++;
    if (c.last_size != 3) failures++;
    if (c.last_first != 7) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL count %0d total %0d", failures, c.total);
  end
endmodule
