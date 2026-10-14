-- fsim: top=vhdl_unconstrained_function_result_procedure_batch58(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0
-- Keep unconstrained function results as direct actuals to unconstrained
-- procedure formals; no constrained object supplies a result width.
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_unconstrained_function_result_procedure_batch58 is
end entity;

architecture sim of vhdl_unconstrained_function_result_procedure_batch58 is
  function vector_xor_scalar(
    value : std_logic_vector;
    scalar_value : std_logic
  ) return std_logic_vector is
  begin
    return value xor scalar_value;
  end function;

  function scalar_xor_vector(
    scalar_value : std_logic;
    value : std_logic_vector
  ) return std_logic_vector is
  begin
    return scalar_value xor value;
  end function;

  procedure check_result(
    actual : in std_logic_vector;
    expected : in std_logic_vector;
    expected_left : in integer;
    expected_right : in integer;
    case_name : in string
  ) is
  begin
    assert actual'length = expected'length
      report "FAIL " & case_name & " returned wrong length"
      severity failure;
    assert actual'left = expected_left and actual'right = expected_right
      report "FAIL " & case_name & " returned wrong bounds"
      severity failure;
    assert actual'left = expected'left and actual'right = expected'right
      report "FAIL " & case_name & " bounds disagree with expected value"
      severity failure;

    if actual'left = expected'left and actual'right = expected'right then
      for index in expected'range loop
        assert actual(index) = expected(index)
          report "FAIL " & case_name & " returned wrong element"
          severity failure;
      end loop;
    end if;
  end procedure;

  procedure consume_nested(
    actual : in std_logic_vector;
    expected : in std_logic_vector;
    expected_left : in integer;
    expected_right : in integer;
    case_name : in string
  ) is
  begin
    -- Re-pass the unconstrained formal without assigning it to a constrained
    -- intermediate object.
    check_result(actual, expected, expected_left, expected_right, case_name);
  end procedure;

  procedure check_unrelated_width(
    actual : in std_logic_vector;
    unrelated : in std_logic_vector;
    expected : in std_logic_vector
  ) is
  begin
    assert actual'length = 3
      report "FAIL function result was forced to an unrelated width"
      severity failure;
    assert unrelated'length = 5
      report "FAIL unrelated width control is not five elements"
      severity failure;
    assert actual'length /= unrelated'length
      report "FAIL unconstrained function result was coerced to other actual"
      severity failure;
    check_result(actual, expected, 1, 3, "unrelated-width result");
  end procedure;
  procedure check_array_overload(actual : in std_logic_vector) is
  begin
    assert actual'length = 1 and actual'left = 1 and actual'right = 1
      report "FAIL one-element function result lost its array bounds"
      severity failure;
    assert actual(1) = '1' severity failure;
  end procedure;

  procedure check_array_overload(actual : in std_logic) is
  begin
    assert false report "FAIL array result matched scalar overload"
      severity failure;
  end procedure;
begin
  exercise : process
    variable descending_three : std_logic_vector(5 downto 3);
    variable ascending_five : std_logic_vector(2 to 6);
    variable expected_three : std_logic_vector(1 to 3);
    variable expected_five : std_logic_vector(1 to 5);
    variable unrelated_five : std_logic_vector(1 to 5);
    variable scalar_value : std_logic;
    variable single_value : std_logic_vector(9 downto 9);
  begin
    descending_three := "101";
    ascending_five := "10110";
    scalar_value := '1';
    expected_three := "010";
    expected_five := "01001";
    unrelated_five := "00000";

    consume_nested(
      vector_xor_scalar(descending_three, scalar_value),
      expected_three, 1, 3, "descending vector xor scalar");
    consume_nested(
      scalar_xor_vector(scalar_value, descending_three),
      expected_three, 1, 3, "descending scalar xor vector");
    consume_nested(
      vector_xor_scalar(ascending_five, scalar_value),
      expected_five, 1, 5, "ascending vector xor scalar");
    consume_nested(
      scalar_xor_vector(scalar_value, ascending_five),
      expected_five, 1, 5, "ascending scalar xor vector");

    -- Change runtime inputs and verify a three-element result remains three
    -- elements when another unconstrained actual has five elements.
    descending_three := "011";
    scalar_value := '0';
    expected_three := "011";
    check_unrelated_width(
      vector_xor_scalar(descending_three, scalar_value),
      unrelated_five,
      expected_three);

    single_value := "1";
    check_array_overload(vector_xor_scalar(single_value, scalar_value));

    report "PASS" severity note;
    wait;
  end process;
end architecture;
