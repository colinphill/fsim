-- fsim: top=vhdl_chained_function_result_indexing(sim) std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-1993 6.1: a function result may be followed by an indexed name;
-- each index selects one dimension of the returned array.
entity vhdl_chained_function_result_indexing is
end entity;

architecture sim of vhdl_chained_function_result_indexing is
  type matrix_t is array (1 to 2, 1 to 3) of integer;
  constant matrix_value : matrix_t := ((1, 2, 3), (4, 5, 6));

  function F2(A : integer) return matrix_t is
  begin
    return matrix_value;
  end function;
begin
  process
  begin
    if F2(0)(2, 3) /= 6 then
      report "FAIL chained function result indexing" severity error;
    else
      report "PASS";
    end if;
    wait;
  end process;
end architecture;
