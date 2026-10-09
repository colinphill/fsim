// fsim: top=sv_real_values_and_events std=2017
// IEEE 1800-2017 6.12.2: integral and real values convert on assignment to
// container elements and function arguments; 11.3.1: a real operand makes an
// operation real, and 11.4.3: a real power; 11.4.13: compound assignments
// and increments of real variables and array elements; 9.4.2: an event
// control on a real array element; 10.4.2: a nonblocking assignment with an
// intra-assignment event control does not block its process; 10.6.2: a
// released variable keeps its forced value; 23.6: a hierarchical reference to
// an instance parameter; 13.4.5: a built-in method without parentheses;
// 21.2.3: $monitor arguments that are expressions.
module sv_real_values_and_events_child #(parameter int WIDTH = 4,
    parameter real SCALE = 0.5) ();
endmodule

module sv_real_values_and_events;
  real dyn[];
  shortreal single[];
  real fixed_array[2];
  real watched[2];
  real seen;
  integer i = 7;
  int iq[$];
  int failures = 0;
  time unset_time;
  int held;
  int delayed = 1;
  reg go = 0;

  sv_real_values_and_events_child #(.WIDTH(6), .SCALE(2.5)) child();

  function automatic real plus_three(input real x);
    plus_three = x + 3;
  endfunction

  function automatic int truncate(input int x);
    truncate = x;
  endfunction

  always @(watched[0]) seen = watched[0];

  task check_real(input real got, input real expected, input string what);
    if (got != expected) begin
      $display("FAIL %s: got %f expected %f", what, got, expected);
      failures++;
    end
  endtask

  task check(input int got, input int expected, input string what);
    if (got != expected) begin
      $display("FAIL %s: got %0d expected %0d", what, got, expected);
      failures++;
    end
  endtask

  initial begin
    real r;
    dyn = new[3];
    single = new[2];
    dyn[1] = i;
    single[0] = 5;
    fixed_array[1] = 3;
    check_real(dyn[1] + 1, 8.0, "integral to real element");
    check_real(single[0], 5.0, "integral to shortreal element");
    check_real(fixed_array[1], 3.0, "integral to fixed real element");
    fixed_array[0] = 8.0;
    fixed_array[0] += 1.0;
    fixed_array[0] *= 2.0;
    fixed_array[0]--;
    check_real(fixed_array[0], 17.0, "real element updates");
    r = 5;
    r++;
    r /= 4;
    check_real(r, 1.5, "real variable updates");
    check_real(plus_three(i), 10.0, "integral argument to real formal");
    check_real(plus_three(5), 8.0, "constant argument to real formal");
    check(truncate(2.6), 3, "real argument to integral formal");
    check_real(2.0 ** 3, 8.0, "real power");
    check_real(unset_time / 2.0, 0.0, "unknown time in a real operation");
    #10;
    check_real($time / 4.0, 2.5, "time divided by real");
    watched[0] = 1.25;
    #1;
    check_real(seen, 1.25, "event on a real array element");
    delayed <= @(posedge go) 2;
    check(delayed, 1, "nonblocking event assignment does not block");
    #1 go = 1;
    #1;
    check(delayed, 2, "nonblocking event assignment update");
    held = 1;
    force held = 4;
    release held;
    check(held, 4, "released variable keeps its forced value");
    check(child.WIDTH, 6, "hierarchical parameter reference");
    check_real(child.SCALE, 2.5, "hierarchical real parameter");
    iq.push_back(4);
    iq.push_back(9);
    check(iq.size, 2, "size without parentheses");
    check(iq.pop_back, 9, "pop_back without parentheses");
    $monitor("monitor %0d", i + 1);
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
