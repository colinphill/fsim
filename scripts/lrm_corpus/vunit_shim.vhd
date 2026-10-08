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
--   VUNIT-SHIM: fail <test name>: <message>
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
  signal runner : runner_sync_t := runner_idle;

  -- Testbenches are compiled with their runner_cfg generic removed; this
  -- constant takes its place.
  constant runner_cfg : string := "";

  procedure test_runner_setup(
    signal runner : in runner_sync_t;
    constant runner_cfg : in string := "");
  procedure test_runner_cleanup(signal runner : in runner_sync_t);
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
  constant name_capacity : positive := 16384;

  type shim_state_t is protected
    procedure begin_suite;
    impure function next_iteration return boolean;
    impure function try_run(constant name : string) return boolean;
    procedure record_failure(constant msg : string);
    impure function failure_count return natural;
  end protected;

  type shim_state_t is protected body
    variable started : boolean := false;
    variable ran_in_iteration : boolean := false;
    variable done_names : string(1 to name_capacity);
    variable done_length : natural := 0;
    variable current : string(1 to 256);
    variable current_length : natural := 0;
    variable failures : natural := 0;

    procedure begin_suite is
    begin
      current(1 to 9) := "<default>";
      current_length := 9;
    end procedure;

    impure function next_iteration return boolean is
    begin
      if not started then
        started := true;
        ran_in_iteration := false;
        return true;
      end if;
      if ran_in_iteration then
        ran_in_iteration := false;
        return true;
      end if;
      return false;
    end function;

    impure function already_ran(constant name : string) return boolean is
      constant key : string := "|" & name & "|";
    begin
      if done_length < key'length then
        return false;
      end if;
      for start in 1 to done_length - key'length + 1 loop
        if done_names(start to start + key'length - 1) = key then
          return true;
        end if;
      end loop;
      return false;
    end function;

    impure function try_run(constant name : string) return boolean is
      constant key : string := "|" & name & "|";
    begin
      if ran_in_iteration or already_ran(name) then
        return false;
      end if;
      if done_length + key'length <= name_capacity then
        done_names(done_length + 1 to done_length + key'length) := key;
        done_length := done_length + key'length;
      end if;
      ran_in_iteration := true;
      current_length := name'length;
      if current_length > current'length then
        current_length := current'length;
      end if;
      current(1 to current_length) := name(name'left to name'left + current_length - 1);
      report "VUNIT-SHIM: start " & name severity note;
      return true;
    end function;

    procedure record_failure(constant msg : string) is
    begin
      failures := failures + 1;
      report "VUNIT-SHIM: fail " & current(1 to current_length) & ": " & msg
        severity note;
    end procedure;

    impure function failure_count return natural is
    begin
      return failures;
    end function;
  end protected body;

  shared variable state : shim_state_t;

  procedure test_runner_setup(
    signal runner : in runner_sync_t;
    constant runner_cfg : in string := "") is
  begin
    state.begin_suite;
  end procedure;

  procedure test_runner_cleanup(signal runner : in runner_sync_t) is
  begin
    report "VUNIT-SHIM: done failures=" & integer'image(state.failure_count)
      severity note;
    std.env.finish;
  end procedure;

  impure function test_suite return boolean is
  begin
    return state.next_iteration;
  end function;

  impure function run(constant name : string) return boolean is
  begin
    return state.try_run(name);
  end function;

  procedure info(constant msg : in string) is
  begin
    report "VUNIT-SHIM: info " & msg severity note;
  end procedure;

  procedure warning(constant msg : in string) is
  begin
    report "VUNIT-SHIM: warning " & msg severity note;
  end procedure;

  procedure error(constant msg : in string) is
  begin
    state.record_failure("error: " & msg);
  end procedure;

  procedure failure(constant msg : in string) is
  begin
    state.record_failure("failure: " & msg);
  end procedure;

  procedure check_result(
    constant pass : in boolean;
    constant what : in string;
    constant msg : in string) is
  begin
    if not pass then
      state.record_failure(what & " " & msg);
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
