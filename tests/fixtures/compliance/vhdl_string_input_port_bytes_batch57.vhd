-- fsim: top=vhdl_string_input_port_bytes_batch57(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- VHDL STRING input ports retain every CHARACTER byte, including NUL, while
-- unconstrained formals keep the actual array's length and index direction.
entity vhdl_string_input_port_bytes_batch57 is
  port (
    ascending_text : in string(1 to 3) := "abc";
    descending_text : in string(3 downto 1) := "xyz";
    embedded_nul_text : in string(1 to 2) :=
      (1 => character'val(0), 2 => 'A');
    null_text : in string(1 to 0) := ""
  );
end entity;

architecture sim of vhdl_string_input_port_bytes_batch57 is
  constant embedded_nul_expected : string(1 to 2) :=
    (1 => character'val(0), 2 => 'A');

  procedure check_text(
    value : in string;
    expected : in string;
    expected_left : in integer;
    expected_right : in integer;
    expected_ascending : in boolean;
    expected_length : in integer;
    result : out boolean) is
  begin
    result := value = expected
      and value'left = expected_left
      and value'right = expected_right
      and value'ascending = expected_ascending
      and value'length = expected_length;
  end procedure;
begin
  test : process
    variable result : boolean;
  begin
    check_text(ascending_text, "abc", 1, 3, true, 3, result);
    assert result report "FAIL ascending STRING port bytes or bounds"
      severity failure;
    check_text(descending_text, "xyz", 3, 1, false, 3, result);
    assert result report "FAIL descending STRING port bytes or bounds"
      severity failure;
    check_text(
      embedded_nul_text, embedded_nul_expected, 1, 2, true, 2, result);
    assert result report "FAIL embedded NUL in STRING port"
      severity failure;
    check_text(null_text, "", 1, 0, true, 0, result);
    assert result report "FAIL null STRING port bounds or length"
      severity failure;
    assert false report "PASS" severity note;
    wait;
  end process;
end architecture;
