-- fsim: top=logical_array_rank_controls(rtl) std=2008
-- SPDX-License-Identifier: Apache-2.0
entity logical_array_rank_controls is
end entity;

architecture rtl of logical_array_rank_controls is
  type row_type is array (2 downto 0) of boolean;
  type matrix is array (1 to 1, 1 to 1) of boolean;

  function "and" (left, right : matrix) return matrix is
  begin
    return left;
  end function;

  function "not" (value : matrix) return matrix is
  begin
    return value;
  end function;
begin
  process
    variable left_row : row_type := (true, false, true);
    variable right_row : row_type := (false, true, true);
    variable result_row : row_type;
    variable left_matrix : matrix := (1 => (1 => true));
    variable right_matrix : matrix := (1 => (1 => false));
    variable result_matrix : matrix;
  begin
    result_row := left_row xor right_row;
    assert result_row = row_type'(true, true, false) severity failure;
    result_row := not left_row;
    assert result_row = row_type'(false, true, false) severity failure;
    result_matrix := left_matrix and right_matrix;
    assert result_matrix(1, 1) severity failure;
    result_matrix := not right_matrix;
    assert not result_matrix(1, 1) severity failure;
    assert not false severity failure;
    report "PASS";
    wait;
  end process;
end architecture;
