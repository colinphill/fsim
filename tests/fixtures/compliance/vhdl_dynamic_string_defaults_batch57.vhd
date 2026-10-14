-- fsim: top=vhdl_dynamic_string_defaults_batch57(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- Formal-bound STRING locals preserve their runtime extent and NUL defaults.
use std.textio.all;

entity vhdl_dynamic_string_defaults_batch57 is
end entity;

architecture sim of vhdl_dynamic_string_defaults_batch57 is
  procedure check_ascending(
    right_bound : positive;
    expected_length : natural
  ) is
    variable value : string(2 to right_bound);
  begin
    assert value'length = expected_length
      report "FAIL ascending dynamic STRING extent" severity failure;
    if expected_length = 0 then
      assert value = ""
        report "FAIL ascending dynamic null STRING" severity failure;
    else
      for index in value'range loop
        assert value(index) = NUL
          report "FAIL ascending dynamic STRING element default"
          severity failure;
      end loop;
      assert value(value'left) = NUL
        and value(value'right) = NUL
        report "FAIL ascending dynamic STRING default" severity failure;
    end if;
  end procedure;

  procedure check_descending(
    right_bound : positive;
    expected_length : natural
  ) is
    variable value : string(4 downto right_bound);
  begin
    assert value'length = expected_length
      report "FAIL descending dynamic STRING extent" severity failure;
    if expected_length = 0 then
      assert value = ""
        report "FAIL descending dynamic null STRING" severity failure;
    else
      for index in value'range loop
        assert value(index) = NUL
          report "FAIL descending dynamic STRING element default"
          severity failure;
      end loop;
      assert value(value'left) = NUL
        and value(value'right) = NUL
        report "FAIL descending dynamic STRING default" severity failure;
    end if;
  end procedure;

  procedure check_extreme_null_ranges(
    ascending_left : positive;
    ascending_right : positive;
    descending_left : positive;
    descending_right : positive
  ) is
    variable ascending_value : string(ascending_left to ascending_right);
    variable descending_value : string(descending_left downto descending_right);
  begin
    assert ascending_value'length = 0 and ascending_value = ""
      report "FAIL ascending extreme null STRING" severity failure;
    assert descending_value'length = 0 and descending_value = ""
      report "FAIL descending extreme null STRING" severity failure;
  end procedure;

  procedure check_initialized_ascending(
    right_bound : positive;
    expected_value : string
  ) is
    variable value : string(2 to right_bound) := expected_value;
  begin
    assert value = expected_value
      report "FAIL ascending initialized dynamic STRING" severity failure;
  end procedure;

  procedure check_initialized_descending(
    right_bound : positive;
    expected_value : string
  ) is
    variable value : string(4 downto right_bound) := expected_value;
  begin
    assert value = expected_value
      report "FAIL descending initialized dynamic STRING" severity failure;
  end procedure;

  procedure check_read_after_assignment(
    right_bound : positive;
    source_value : string;
    expected_read : string;
    remaining : string
  ) is
    variable line_value : line;
    variable value : string(2 to right_bound);
    variable good : boolean := false;
  begin
    value := source_value;
    line_value := new string'("abcdef");
    read(line_value, value, good);
    assert good and value = expected_read and line_value.all = remaining
      report "FAIL READ used the wrong constrained STRING length"
      severity failure;
    deallocate(line_value);
  end procedure;
begin
  process
  begin
    check_ascending(4, 3);
    check_ascending(6, 5);
    check_ascending(1, 0);
    check_descending(2, 3);
    check_descending(1, 4);
    check_descending(5, 0);
    check_extreme_null_ranges(
      positive'high, positive'low, positive'low, positive'high);
    check_initialized_ascending(4, "abc");
    check_initialized_ascending(6, "hello");
    check_initialized_ascending(1, "");
    check_initialized_descending(2, "abc");
    check_initialized_descending(1, "wxyz");
    check_initialized_descending(5, "");
    check_read_after_assignment(4, "xyz", "abc", "def");
    check_read_after_assignment(6, "hello", "abcde", "f");
    report "PASS";
    wait;
  end process;
end architecture;
