-- fsim: top=vhdl_procedure_string_actual_range_batch58(sim) std=2008
-- SPDX-License-Identifier: Apache-2.0

entity vhdl_procedure_string_actual_range_batch58 is
end entity;

architecture sim of vhdl_procedure_string_actual_range_batch58 is
  subtype descending_text is string(7 downto 5);
  constant descending_actual : descending_text := "xyz";
begin
  check_actual_ranges : process
    variable result : boolean;

    procedure check_range(
      value : in string;
      expected_left : in integer;
      expected_right : in integer;
      expected_ascending : in boolean;
      passed : out boolean
    ) is
      variable visited : natural := 0;
    begin
      visited := 0;
      for index in value'range loop
        visited := visited + 1;
      end loop;
      passed := value'left = expected_left
        and value'right = expected_right
        and value'ascending = expected_ascending
        and visited = value'length;
    end procedure;

    procedure check_default(
      value : in string := descending_actual;
      passed : out boolean
    ) is
    begin
      passed := value'left = 7
        and value'right = 5
        and not value'ascending;
    end procedure;
  begin
    check_range("VHDL", 1, 4, true, result);
    assert result
      report "FAIL unconstrained STRING literal actual lost its bounds"
      severity failure;

    check_range("", 1, 0, true, result);
    assert result
      report "FAIL null STRING actual lost its null ascending range"
      severity failure;

    check_range(descending_actual, 7, 5, false, result);
    assert result
      report "FAIL constrained descending STRING actual lost its direction"
      severity failure;

    check_default(passed => result);
    assert result
      report "FAIL default STRING actual lost its bounds"
      severity failure;

    assert false report "PASS" severity note;
    wait;
  end process;
end architecture;
