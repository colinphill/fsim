// fsim: top=sv_package_timescale_and_returns std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 3.14.2: a subroutine declared in a package uses the
// package's time unit for $time, not the caller's.
// 13.4.1 and 6.12.2: a return value converts to the function's declared
// type, rounding a real to the nearest integer.
package sv_package_timescale_pkg;
  timeunit 100ps / 10ps;
  task automatic stamp(output longint t);
    #5ns t = $time;
  endtask
endpackage

module sv_package_timescale_and_returns;
  timeunit 1ns / 1ps;
  function automatic int rounded(input real value);
    return value;
  endfunction
  function automatic logic [7:0] wrapped();
    return -2.5;
  endfunction
  function automatic real widened(input int value);
    return value;
  endfunction
  longint stamped, caller;
  int failures;
  initial begin
    sv_package_timescale_pkg::stamp(stamped);
    caller = $time;
    if (stamped != 50 || caller != 5) failures++;
    if (rounded(2.6) != 3 || rounded(-1.5) != -2) failures++;
    if (wrapped() != 8'd253) failures++;
    if (widened(7) != 7.0) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL %0d %0d %0d", failures, stamped, caller);
  end
endmodule
