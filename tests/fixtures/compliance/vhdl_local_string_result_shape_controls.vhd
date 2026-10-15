-- fsim: top=vhdl_local_string_result_shape_controls(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- Local-result STRING shape controls for nested, null, and constrained actuals.
entity vhdl_local_string_result_shape_controls is
end entity;

architecture sim of vhdl_local_string_result_shape_controls is
  function join(left_value : string; right_value : string) return string is
    variable result : string(1 to left_value'length + right_value'length);
    variable destination_index : integer := result'left;
  begin
    for index in left_value'range loop
      result(destination_index) := left_value(index);
      destination_index := destination_index + 1;
    end loop;
    for index in right_value'range loop
      result(destination_index) := right_value(index);
      destination_index := destination_index + 1;
    end loop;
    return result;
  end function;

  function join_descending(left_value : string; right_value : string)
      return string is
    variable result : string(left_value'length + right_value'length downto 1);
    variable destination_index : integer := result'left;
  begin
    for index in left_value'range loop
      result(destination_index) := left_value(index);
      destination_index := destination_index - 1;
    end loop;
    for index in right_value'range loop
      result(destination_index) := right_value(index);
      destination_index := destination_index - 1;
    end loop;
    return result;
  end function;
begin
  check : process
    constant descending_text : string(7 downto 6) := "AB";
    constant ascending_text : string(3 to 4) := "CD";
    variable nested_text : string(1 to 5);
    variable empty_text : string(1 to 0);
    variable descending_result : string(3 downto 1);
    variable static_result : string(1 to 4);
  begin
    nested_text := join(join("A", "BC"), join("D", "E"));
    assert nested_text = "ABCDE"
      report "nested local-result STRING calls lost their result bounds"
      severity failure;
    empty_text := join("", "");
    assert empty_text'length = 0
        and join("", "X") = "X"
        and join("X", "") = "X"
      report "null STRING actuals did not preserve local result lengths"
      severity failure;
    descending_result := join_descending("A", "BC");
    assert descending_result = "ABC"
        and descending_result'left = 3
        and descending_result'right = 1
        and not descending_result'ascending
      report "descending local-result STRING bounds were not retained"
      severity failure;
    static_result := join(descending_text, ascending_text);
    assert static_result = "ABCD"
        and static_result'left = 1
        and static_result'right = 4
      report "constrained STRING actual ranges changed concatenated content"
      severity failure;
    report "PASS" severity note;
    wait;
  end process;
end architecture;
