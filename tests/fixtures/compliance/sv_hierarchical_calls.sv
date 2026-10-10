// fsim: top=sv_hierarchical_calls
// IEEE 1800-2017 23.8: functions and tasks of other module instances and of
// generate blocks called through hierarchical names, reading and writing
// the variables of the instance that declares them.
module counter;
  int count = 0;
  function automatic int scaled(input int factor);
    return count * factor;
  endfunction
  task automatic bump(input int by);
    count += by;
  endtask
endmodule

module sv_hierarchical_calls;
  counter u();
  if (1) begin : blk
    function automatic int twice(input int value);
      return value * 2;
    endfunction
  end

  initial begin
    int failures = 0;
    u.bump(3);
    u.bump(4);
    if (u.count != 7) failures++;
    if (u.scaled(2) != 14) failures++;
    if (blk.twice(5) != 10) failures++;
    if (sv_hierarchical_calls.u.scaled(1) != 7) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL %0d", failures);
  end
endmodule
