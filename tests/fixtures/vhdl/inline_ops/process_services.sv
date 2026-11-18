// SPDX-License-Identifier: Apache-2.0
//
// Process handles and the time format in the v4 kernel's generated code
// (IEEE 1800-2023 9.7, 18.13.3 - 18.13.5, 20.4.2): process::self of a
// thread and a fork child, their status, a random state taken, drawn from,
// restored and drawn again (the same number), srandom giving a child the
// same stream as a second srandom with that seed, and $timeformat changing
// how %t formats.
//   t = 1500 ns: %t first gives 1500000 (picoseconds, the precision) in 20
//   columns, then 1.5 us in 9.
module top;
  timeunit 1ns; timeprecision 1ps;
  process me, child;
  string state;
  int first, second, a1, a2;
  time t;
  initial begin
    me = process::self();
    $display("inl self %0d", me.status() == process::RUNNING);
    state = me.get_randstate();
    first = $urandom;
    me.set_randstate(state);
    second = $urandom;
    $display("inl randstate %0d", first == second);
    fork
      begin
        child = process::self();
        child.srandom(7);
        a1 = $urandom;
        child.srandom(7);
        a2 = $urandom;
      end
    join
    $display("inl srandom %0d child %0d", a1 == a2, child.status() == process::FINISHED);
    t = 1500;
    $display("inl time [%t]", t);
    $timeformat(-6, 1, " us", 9);
    $display("inl time [%t]", t);
  end
endmodule
