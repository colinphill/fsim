// fsim: top=sv_deferred_assertion_capture
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 16.4 and 16.4.1: the actuals of an observed deferred
// assertion's action that are passed by value, "including function calls,
// shall be fully evaluated at the instant the deferred assertion expression
// is evaluated"; the call and "the current values of its input arguments"
// are queued, and the call runs in the Reactive region.  An actual passed to
// a ref formal uses the current value of its variable in the Reactive region.
//
// Part 1 (time 0): the assertion passes with x == 1; every later assignment
// below runs in the same Active region, before the Reactive region runs the
// queued call.  The call therefore sees the values at assertion time:
//   a = x = 1, b = y + 1 = 3, str = s = "one", f = next_value() = 10 (the
//   function ran once, at the assertion, so fcalls == 1), w = wide as first
//   assigned, p = pair as first assigned.
// The ref actual `shared` is read in the Reactive region: 77, its last value.
// Part 2 (time 1): the assertion fails with x == 4, so the else call is
// queued with the values then (x = 4, y = 5, "four", next_value() = 20); the
// assignments after it do not reach the call.  log_text receives
// $sformatf("x=%0d s=%s", x, s) evaluated at assertion time: "x=4 s=four".
// The $display action prints "cap x=4 y=5 s=four" in the Reactive region
// (printed line, not checked by the self-check).
module sv_deferred_assertion_capture;
  timeunit 1ns; timeprecision 1ns;
  typedef struct packed { logic [7:0] hi; logic [7:0] lo; } pair_t;
  int x, y, fcalls, calls, failures;
  int shared = 5;
  string s;
  logic [127:0] wide;
  pair_t pair;
  int r_a, r_b, r_f, r_ref;
  string r_s, r_text;
  logic [127:0] r_wide;
  pair_t r_pair;

  function automatic int next_value();
    fcalls++;
    return fcalls * 10;
  endfunction

  task automatic record(input int a, input int b, input string str,
      input int f, input logic [127:0] w, input pair_t p, ref int live);
    r_a = a; r_b = b; r_s = str; r_f = f; r_wide = w; r_pair = p;
    r_ref = live;
    calls++;
  endtask

  task automatic log_text(input string text);
    r_text = text;
  endtask

  initial begin
    x = 1; y = 2; s = "one";
    wide = 128'h0123_4567_89ab_cdef_0011_2233_4455_6677;
    pair = '{hi: 8'hA1, lo: 8'hB2};
    shared = 5;
    assert #0 (x == 1) record(x, y + 1, s, next_value(), wide, pair, shared);
    x = 100; y = 200; s = "two"; wide = ~wide;
    pair = '{hi: 8'h00, lo: 8'hFF};
    shared = 77;
    #1;
    if (calls != 1) failures++;
    if (r_a != 1 || r_b != 3) failures++;
    if (r_s != "one") failures++;
    if (r_f != 10 || fcalls != 1) failures++;
    if (r_wide != 128'h0123_4567_89ab_cdef_0011_2233_4455_6677) failures++;
    if (r_pair.hi != 8'hA1 || r_pair.lo != 8'hB2) failures++;
    if (r_ref != 77) failures++;

    x = 4; y = 5; s = "four";
    assert #0 (x == 0) $display("unused pass");
      else record(x, y, s, next_value(), wide, pair, shared);
    assert #0 (x == 4) log_text($sformatf("x=%0d s=%s", x, s));
    assert #0 (x == 4) $display("cap x=%0d y=%0d s=%s", x, y, s);
    x = 9; y = 9; s = "nine"; wide = '0;
    pair = '{hi: 8'h11, lo: 8'h22};
    shared = 1;
    #1;
    if (calls != 2) failures++;
    if (r_a != 4 || r_b != 5) failures++;
    if (r_s != "four") failures++;
    if (r_f != 20 || fcalls != 2) failures++;
    if (r_wide != ~128'h0123_4567_89ab_cdef_0011_2233_4455_6677) failures++;
    if (r_pair.hi != 8'h00 || r_pair.lo != 8'hFF) failures++;
    if (r_ref != 1) failures++;
    if (r_text != "x=4 s=four") failures++;

    if (failures == 0) $display("PASS");
    else $display("FAIL count %0d", failures);
  end
endmodule
