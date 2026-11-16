// fsim: top=sv_kernel_string_variables std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 6.16, 21.6, D.8: string variables of a module and of a
// package read and written in place by compiled kernel code (short and
// long values, the package's initializer and function, a nonblocking
// write maturing in the NBA region), $test$plusargs with no matching
// plusarg, and $reset_count/$reset_value of a first run.
package sv_kernel_string_variables_pkg;
  string greeting = {"hel", "lo"};
  string shared;
  function automatic string exclaim();
    return {greeting, "!"};
  endfunction
endpackage

module sv_kernel_string_variables;
  import sv_kernel_string_variables_pkg::*;
  int failures;
  string s;
  string u;
  string q;

  task automatic check(input string what, input string got, input string want);
    if (got != want) begin
      $display("FAIL %s: got \"%s\" want \"%s\"", what, got, want);
      failures++;
    end
  endtask

  task automatic check_int(input string what, input int got, input int want);
    if (got != want) begin
      $display("FAIL %s: got %0d want %0d", what, got, want);
      failures++;
    end
  endtask

  initial begin
    check("package initializer", greeting, "hello");
    s = "module";
    u = {s, "-", greeting};
    check("concatenated", u, "module-hello");
    s = "a string of more than fifteen bytes, quite long indeed";
    u = s;
    check_int("long length", u.len(), 54);
    shared = exclaim();
    check("package function", shared, "hello!");
    s = "";
    check_int("emptied", s.len(), 0);
    s <= "nonblocking";
    check("before the NBA region", u, "a string of more than fifteen bytes, quite long indeed");
    check_int("not yet", s.len(), 0);
    #1;
    check("after the NBA region", s, "nonblocking");
    q = "SV_KERNEL_STRING_VARIABLES_NOT_GIVEN";
    check_int("no plusarg", $test$plusargs(q), 0);
    check_int("no literal plusarg", $test$plusargs("SV_KERNEL_NOT_GIVEN"), 0);
    check_int("reset count", $reset_count, 0);
    check_int("reset value", $reset_value, 0);
    if (failures == 0) $display("PASS");
    $finish;
  end
endmodule
