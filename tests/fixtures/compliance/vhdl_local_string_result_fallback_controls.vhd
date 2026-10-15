-- fsim: top=vhdl_local_string_result_fallback_controls(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- Valid cases that must use ordinary lowering when the bounded proof declines.
entity vhdl_local_string_result_fallback_controls is
end entity;

architecture sim of vhdl_local_string_result_fallback_controls is
  type marker_t is (marker_a, marker_b);

  function "+" (left_value : marker_t; right_value : marker_t)
      return marker_t is
  begin
    if left_value = marker_a then
      return right_value;
    end if;
    return left_value;
  end function;

  function join_local(left_value : string; right_value : string)
      return string is
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

  function early_or_local(choose_early : boolean; value : string)
      return string is
    variable result : string(1 to value'length);
    variable destination_index : integer := result'left;
  begin
    if choose_early then
      return "EARLY";
    end if;
    for index in value'range loop
      result(destination_index) := value(index);
      destination_index := destination_index + 1;
    end loop;
    return result;
  end function;
begin
  check : process
    variable joined : string(1 to 2);
    variable selected : string(1 to 5);
    variable copied : string(1 to 1);
  begin
    joined := join_local("A", "B");
    assert joined = "AB"
      report "overloaded unrelated + broke ordinary STRING lowering"
      severity failure;
    selected := early_or_local(true, "Q");
    assert selected = "EARLY"
      report "valid early STRING return was not preserved"
      severity failure;
    copied := early_or_local(false, "Q");
    assert copied = "Q"
      report "final local STRING return was not preserved"
      severity failure;
    report "PASS" severity note;
    wait;
  end process;
end architecture;
