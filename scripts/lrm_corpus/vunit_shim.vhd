-- SPDX-License-Identifier: Apache-2.0
--
-- Minimal VUnit-compatible test API for running VUnit-style compliance
-- testbenches (for example VHDL/Compliance-Tests) under fsim without the VUnit
-- Python runner. It is compiled into library vunit_lib by
-- scripts/lrm_corpus.py.
--
-- Every test case in a testbench runs once, in source order, in a single
-- simulation. Check failures do not stop the simulation; they are logged as
-- machine-readable lines that the corpus runner attributes to the active test:
--
--   VUNIT-SHIM: start <test name>
--   VUNIT-SHIM: fail: <message>        (for the most recently started test)
--   VUNIT-SHIM: done failures=<count>
--
-- Only the subset of the VUnit API used by the corpora is provided. Anything
-- else fails to analyze, which the runner reports as a harness gap rather than
-- an fsim gap.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package vunit_shim_pkg is
  type runner_sync_t is (runner_idle, runner_active);
  -- VUnit's runner is a synchronization signal; testbenches only pass it to
  -- test_runner_setup/cleanup, so a constant stands in for it.
  constant runner : runner_sync_t := runner_idle;

  -- Testbenches are compiled with their runner_cfg generic removed; this
  -- constant takes its place.
  constant runner_cfg : string := "";

  procedure test_runner_setup(
    constant runner : in runner_sync_t;
    constant runner_cfg : in string := "");
  procedure test_runner_cleanup(constant runner : in runner_sync_t);
  impure function test_suite return boolean;
  impure function run(constant name : string) return boolean;

  procedure info(constant msg : in string);
  procedure warning(constant msg : in string);
  procedure error(constant msg : in string);
  procedure failure(constant msg : in string);

  procedure check(constant expr : in boolean; constant msg : in string := "");
  procedure check_true(constant expr : in boolean; constant msg : in string := "");
  procedure check_false(constant expr : in boolean; constant msg : in string := "");

  procedure check_equal(constant got : in integer; constant expected : in integer; constant msg : in string := "");
  procedure check_equal(constant got : in boolean; constant expected : in boolean; constant msg : in string := "");
  procedure check_equal(constant got : in std_ulogic; constant expected : in std_ulogic; constant msg : in string := "");
  procedure check_equal(constant got : in std_ulogic; constant expected : in boolean; constant msg : in string := "");
  procedure check_equal(constant got : in boolean; constant expected : in std_ulogic; constant msg : in string := "");
  procedure check_equal(constant got : in bit; constant expected : in bit; constant msg : in string := "");
  procedure check_equal(constant got : in character; constant expected : in character; constant msg : in string := "");
  procedure check_equal(constant got : in string; constant expected : in string; constant msg : in string := "");
  procedure check_equal(constant got : in time; constant expected : in time; constant msg : in string := "");
  procedure check_equal(constant got : in real; constant expected : in real; constant msg : in string := "");
  procedure check_equal(constant got : in bit_vector; constant expected : in bit_vector; constant msg : in string := "");
  procedure check_equal(constant got : in std_ulogic_vector; constant expected : in std_ulogic_vector; constant msg : in string := "");
  procedure check_equal(constant got : in unsigned; constant expected : in unsigned; constant msg : in string := "");
  procedure check_equal(constant got : in unsigned; constant expected : in natural; constant msg : in string := "");
  procedure check_equal(constant got : in natural; constant expected : in unsigned; constant msg : in string := "");
  procedure check_equal(constant got : in signed; constant expected : in signed; constant msg : in string := "");
  procedure check_equal(constant got : in signed; constant expected : in integer; constant msg : in string := "");
  procedure check_equal(constant got : in integer; constant expected : in signed; constant msg : in string := "");
end package;

package body vunit_shim_pkg is
  -- Each test_suite iteration runs the next test case: run() counts its
  -- calls within the iteration and selects the call at the current index.
  -- VUnit testbenches call run() from one if/elsif chain, so the order is
  -- the same in every iteration and no test names need to be stored.
  type shim_state_t is protected
    procedure begin_suite;
    impure function next_iteration return boolean;
    impure function try_run return boolean;
    procedure record_failure;
    impure function failure_count return natural;
  end protected;

  type shim_state_t is protected body
    variable started : boolean := false;
    variable ran_in_iteration : boolean := false;
    variable index : natural := 0;
    variable calls : natural := 0;
    variable failures : natural := 0;

    procedure begin_suite is
    begin
      started := false;
    end procedure;

    impure function next_iteration return boolean is
    begin
      if not started then
        started := true;
        index := 1;
      elsif ran_in_iteration then
        index := index + 1;
      else
        return false;
      end if;
      calls := 0;
      ran_in_iteration := false;
      return true;
    end function;

    impure function try_run return boolean is
    begin
      calls := calls + 1;
      if ran_in_iteration or calls /= index then
        return false;
      end if;
      ran_in_iteration := true;
      return true;
    end function;

    procedure record_failure is
    begin
      failures := failures + 1;
    end procedure;

    impure function failure_count return natural is
    begin
      return failures;
    end function;
  end protected body;

  shared variable state : shim_state_t;

  procedure test_runner_setup(
    constant runner : in runner_sync_t;
    constant runner_cfg : in string := "") is
  begin
    state.begin_suite;
  end procedure;

  procedure test_runner_cleanup(constant runner : in runner_sync_t) is
    variable count : natural;
  begin
    count := state.failure_count;
    report "VUNIT-SHIM: done failures=" & integer'image(count) severity note;
    std.env.finish;
  end procedure;

  impure function test_suite return boolean is
  begin
    return state.next_iteration;
  end function;

  impure function run(constant name : string) return boolean is
    variable selected : boolean;
  begin
    selected := state.try_run;
    if selected then
      report "VUNIT-SHIM: start " & name severity note;
    end if;
    return selected;
  end function;

  procedure info(constant msg : in string) is
  begin
    report "VUNIT-SHIM: info " & msg severity note;
  end procedure;

  procedure warning(constant msg : in string) is
  begin
    report "VUNIT-SHIM: warning " & msg severity note;
  end procedure;

  -- Failures are attributed to the most recently started test case.
  procedure fail_with(constant msg : in string) is
  begin
    state.record_failure;
    report "VUNIT-SHIM: fail: " & msg severity note;
  end procedure;

  procedure error(constant msg : in string) is
  begin
    fail_with("error: " & msg);
  end procedure;

  procedure failure(constant msg : in string) is
  begin
    fail_with("failure: " & msg);
  end procedure;

  procedure check_result(
    constant pass : in boolean;
    constant what : in string;
    constant msg : in string) is
  begin
    if not pass then
      fail_with(what & " " & msg);
    end if;
  end procedure;

  procedure check(constant expr : in boolean; constant msg : in string := "") is
  begin
    check_result(expr, "check", msg);
  end procedure;

  procedure check_true(constant expr : in boolean; constant msg : in string := "") is
  begin
    check_result(expr, "check_true", msg);
  end procedure;

  procedure check_false(constant expr : in boolean; constant msg : in string := "") is
  begin
    check_result(not expr, "check_false", msg);
  end procedure;

  procedure check_equal(constant got : in integer; constant expected : in integer; constant msg : in string := "") is
  begin
    check_result(got = expected, "check_equal got " & integer'image(got)
      & " expected " & integer'image(expected), msg);
  end procedure;

  procedure check_equal(constant got : in boolean; constant expected : in boolean; constant msg : in string := "") is
  begin
    check_result(got = expected, "check_equal got " & boolean'image(got)
      & " expected " & boolean'image(expected), msg);
  end procedure;

  procedure check_equal(constant got : in std_ulogic; constant expected : in std_ulogic; constant msg : in string := "") is
  begin
    check_result(got = expected, "check_equal got " & std_ulogic'image(got)
      & " expected " & std_ulogic'image(expected), msg);
  end procedure;

  procedure check_equal(constant got : in std_ulogic; constant expected : in boolean; constant msg : in string := "") is
  begin
    check_result((got = '1') = expected and (got = '0' or got = '1'),
      "check_equal got " & std_ulogic'image(got) & " expected "
      & boolean'image(expected), msg);
  end procedure;

  procedure check_equal(constant got : in boolean; constant expected : in std_ulogic; constant msg : in string := "") is
  begin
    check_result((expected = '1') = got and (expected = '0' or expected = '1'),
      "check_equal got " & boolean'image(got) & " expected "
      & std_ulogic'image(expected), msg);
  end procedure;

  procedure check_equal(constant got : in bit; constant expected : in bit; constant msg : in string := "") is
  begin
    check_result(got = expected, "check_equal got " & bit'image(got)
      & " expected " & bit'image(expected), msg);
  end procedure;

  procedure check_equal(constant got : in character; constant expected : in character; constant msg : in string := "") is
  begin
    check_result(got = expected, "check_equal got " & character'image(got)
      & " expected " & character'image(expected), msg);
  end procedure;

  procedure check_equal(constant got : in string; constant expected : in string; constant msg : in string := "") is
  begin
    check_result(got = expected, "check_equal got """ & got
      & """ expected """ & expected & """", msg);
  end procedure;

  procedure check_equal(constant got : in time; constant expected : in time; constant msg : in string := "") is
  begin
    check_result(got = expected, "check_equal got " & time'image(got)
      & " expected " & time'image(expected), msg);
  end procedure;

  procedure check_equal(constant got : in real; constant expected : in real; constant msg : in string := "") is
  begin
    check_result(got = expected, "check_equal got " & real'image(got)
      & " expected " & real'image(expected), msg);
  end procedure;

  procedure check_equal(constant got : in bit_vector; constant expected : in bit_vector; constant msg : in string := "") is
  begin
    check_result(got = expected, "check_equal got " & to_string(got)
      & " expected " & to_string(expected), msg);
  end procedure;

  procedure check_equal(constant got : in std_ulogic_vector; constant expected : in std_ulogic_vector; constant msg : in string := "") is
  begin
    check_result(got = expected, "check_equal got " & to_string(got)
      & " expected " & to_string(expected), msg);
  end procedure;

  procedure check_equal(constant got : in unsigned; constant expected : in unsigned; constant msg : in string := "") is
  begin
    check_result(got = expected, "check_equal got " & to_string(got)
      & " expected " & to_string(expected), msg);
  end procedure;

  procedure check_equal(constant got : in unsigned; constant expected : in natural; constant msg : in string := "") is
  begin
    check_result(got = expected, "check_equal got " & to_string(got)
      & " expected " & integer'image(expected), msg);
  end procedure;

  procedure check_equal(constant got : in natural; constant expected : in unsigned; constant msg : in string := "") is
  begin
    check_result(got = expected, "check_equal got " & integer'image(got)
      & " expected " & to_string(expected), msg);
  end procedure;

  procedure check_equal(constant got : in signed; constant expected : in signed; constant msg : in string := "") is
  begin
    check_result(got = expected, "check_equal got " & to_string(got)
      & " expected " & to_string(expected), msg);
  end procedure;

  procedure check_equal(constant got : in signed; constant expected : in integer; constant msg : in string := "") is
  begin
    check_result(got = expected, "check_equal got " & to_string(got)
      & " expected " & integer'image(expected), msg);
  end procedure;

  procedure check_equal(constant got : in integer; constant expected : in signed; constant msg : in string := "") is
  begin
    check_result(got = expected, "check_equal got " & integer'image(got)
      & " expected " & to_string(expected), msg);
  end procedure;
end package body;

context vunit_context is
  library vunit_lib;
  use vunit_lib.vunit_shim_pkg.all;
end context;
