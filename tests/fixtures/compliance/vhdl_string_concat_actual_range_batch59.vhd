-- fsim: top=vhdl_string_concat_actual_range_batch59(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
entity vhdl_string_concat_actual_range_batch59 is
end entity;

architecture sim of vhdl_string_concat_actual_range_batch59 is
  subtype reverse_four is string(4 downto 1);
  constant ascending_text : string(4 to 6) := "aba";
  constant descending_text : string(4 downto 2) := "aba";
  constant empty_text : string(3 downto 4) := "";

  function "&"(source_text : string; enabled : boolean)
      return reverse_four is
  begin
    return reverse_four'("abaa");
  end function;

  function "&"(enabled : boolean; source_text : string)
      return reverse_four is
  begin
    return reverse_four'("abaa");
  end function;

  function "&"(left_text : string; right_text : string)
      return boolean is
  begin
    return false;
  end function;

  function count_as(
    value : string;
    expected_left : integer;
    expected_right : integer;
    expected_ascending : boolean;
    expected_count : integer) return integer is
    variable count : integer := 0;
  begin
    assert value'left = expected_left
      report "STRING concatenation actual left bound was incorrect"
      severity failure;
    assert value'right = expected_right
      report "STRING concatenation actual right bound was incorrect"
      severity failure;
    assert value'ascending = expected_ascending
      report "STRING concatenation actual direction was incorrect"
      severity failure;
    if expected_ascending then
      assert value'length = expected_right - expected_left + 1
        report "STRING concatenation actual length was incorrect"
        severity failure;
    else
      assert value'length = expected_left - expected_right + 1
        report "STRING concatenation actual length was incorrect"
        severity failure;
    end if;
    for index in value'range loop
      if value(index) = 'a' then
        count := count + 1;
      end if;
    end loop;
    assert count = expected_count
      report "STRING concatenation actual contents were incorrect"
      severity failure;
    return count;
  end function;
begin
  predefined_cases : process
  begin
    -- These user overloads are visible here. The BOOLEAN-parameter overloads
    -- are inapplicable, and the STRING-parameter overload returns BOOLEAN;
    -- none hides predefined STRING concatenation in this STRING context.
    assert count_as(ascending_text & "baa", 1, 6, true, 4) = 4;
    assert count_as("baa" & ascending_text, 1, 6, true, 4) = 4;
    assert count_as(('b', 'a', 'a') & ascending_text, 1, 6, true, 4) = 4;
    assert count_as(ascending_text & 'a', 1, 4, true, 3) = 3;
    assert count_as('a' & "baa", 1, 4, true, 3) = 3;
    assert count_as("baa" & 'a', 1, 4, true, 3) = 3;
    assert count_as(descending_text & 'a', 1, 4, true, 3) = 3;
    assert count_as(empty_text & descending_text, 1, 3, true, 2) = 2;
    assert count_as(ascending_text, 4, 6, true, 2) = 2;
    assert count_as(descending_text, 4, 2, false, 2) = 2;
    -- The same user overload is selected when the expected result is BOOLEAN.
    assert not (ascending_text & "baa")
      report "BOOLEAN-result overload was not selected in BOOLEAN context"
      severity failure;
    report "PASS" severity note;
    wait;
  end process;

  selected_operator_case : process
  begin
    -- Applicable same-scope overloads return the constrained subtype and
    -- retain its descending bounds in either operand order.
    assert count_as(ascending_text & true, 4, 1, false, 3) = 3;
    assert count_as(false & ascending_text, 4, 1, false, 3) = 3;
    report "PASS" severity note;
    wait;
  end process;
end architecture;
