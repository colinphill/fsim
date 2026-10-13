-- fsim-expect: FSIM-ELAB-VHARRAYSEL-004 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 6.1 and 8.4: an indexed-name group supplies one index
-- for each dimension of the current array subtype.
entity vhdl_partial_function_result_indexing is
end entity;

architecture a of vhdl_partial_function_result_indexing is
  type matrix_t is array (1 to 2, 1 to 2) of integer;
  constant matrix_value : matrix_t := ((1, 2), (3, 4));

  function F(A : integer) return matrix_t is
  begin
    return matrix_value;
  end function;
begin
  process
    variable selected : integer := F(0)(1);
  begin
    wait;
  end process;
end architecture;
