-- fsim: top=vhdl_dynamic_string_range_batch58(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- Dynamic local and proven STRING actual ranges drive loop bounds.
entity vhdl_dynamic_string_range_batch58 is
end entity;

architecture sim of vhdl_dynamic_string_range_batch58 is
  function create_n(size : integer) return string is
    variable result : string(1 to size + 0);
    variable character_value : character := 'A';
  begin
    for index in result'range loop
      result(index) := character_value;
      if character_value /= 'Z' then
        character_value := character'succ(character_value);
      end if;
    end loop;
    return result;
  end function;

  function create_reverse_n(size : integer) return string is
    variable result : string(1 to size * 1);
    variable character_value : character := 'A';
  begin
    for index in result'reverse_range loop
      result(index) := character_value;
      if character_value /= 'Z' then
        character_value := character'succ(character_value);
      end if;
    end loop;
    return result;
  end function;

  function declared_bounds(value : string(1 to 2)) return integer is
  begin
    if value'left = 1 and value'right = 2 and value'ascending then
      return 1;
    end if;
    return 0;
  end function;

  function concat(in1 : string; in2 : string) return string is
    variable result : string(1 to in1'length + in2'length);
    variable destination_index : integer := result'left;
  begin
    for index in in1'range loop
      result(destination_index) := in1(index);
      destination_index := destination_index + 1;
    end loop;
    for index in in2'range loop
      result(destination_index) := in2(index);
      destination_index := destination_index + 1;
    end loop;
    return result;
  end function;
begin
  test : process
    constant descending_text : string(4 downto 3) := "AB";
    constant unconstrained_text : string := "D";
    variable requested_size : integer;
    variable dynamic_text : string(4 downto 2);
  begin
    requested_size := 5;
    dynamic_text := "XYZ";
    assert create_n(0) = ""
      report "zero-length dynamic STRING range was not null" severity failure;
    assert create_n(1) = "A" and create_n(2) = "AB"
        and create_n(5) = "ABCDE"
      report "dynamic local STRING range did not cover its bounds"
      severity failure;
    assert create_n(requested_size) = "ABCDE"
      report "runtime local STRING bound did not drive its range loop"
      severity failure;
    assert create_reverse_n(requested_size) = "EDCBA"
      report "runtime STRING reverse_range did not reverse its direction"
      severity failure;
    assert declared_bounds(descending_text) = 1
      report "constrained STRING formal inherited the actual's bounds"
      severity failure;
    assert concat("ab", "cde") = "abcde"
        and concat("WXYZ", "q") = "WXYZq"
        and concat(descending_text, "!") = "AB!"
        and concat(dynamic_text, "q") = "XYZq"
        and concat(unconstrained_text, "q") = "Dq"
      report "STRING formal ranges were not specialized by actual shape"
      severity failure;
    assert concat("", "x") = "x" and concat("x", "") = "x"
      report "null STRING actuals did not preserve empty formal ranges"
      severity failure;
    report "PASS" severity note;
    wait;
  end process;
end architecture;
