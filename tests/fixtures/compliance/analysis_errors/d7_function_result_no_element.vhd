-- fsim-expect: FSIM-ELAB-VHNAME-014 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 8.3: a selected name whose prefix is a function call
-- selects an element of the record the function returns.
entity d7_function_result_no_element is
end entity;

architecture a of d7_function_result_no_element is
  function f return boolean is
  begin
    return true;
  end function;
begin
  process
    variable b : boolean;
  begin
    b := f.e;
    wait;
  end process;
end architecture;
