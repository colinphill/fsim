-- fsim: top=vhdl_function_result_array_of_arrays_indexing(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 6.1: each parenthesized selector group applies to one
-- array level; chained groups select nested array element subtypes in turn.
entity vhdl_function_result_array_of_arrays_indexing is
end entity;

architecture sim of vhdl_function_result_array_of_arrays_indexing is
  type row_t is array (1 to 2) of integer;
  type matrix_t is array (1 to 2) of row_t;
  constant matrix_value : matrix_t := ((11, 12), (21, 22));

  function F(A : integer) return matrix_t is
  begin
    return matrix_value;
  end function;
begin
  process
    variable row_index : integer := 1;
    variable column_index : integer := 2;
  begin
    if F(0)(row_index)(column_index) /= 12 then
      report "FAIL function result array-of-arrays indexing" severity error;
    else
      report "PASS";
    end if;
    wait;
  end process;
end architecture;
