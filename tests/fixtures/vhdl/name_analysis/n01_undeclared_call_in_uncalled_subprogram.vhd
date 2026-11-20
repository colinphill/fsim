-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 12.3/12.4: every name is resolved at analysis, whether or not the subprogram is ever called.
-- Expected: FSIM-VHDL-NAME-001 at line 9 (no visible declaration of "no_such_function").
entity n01 is
end entity;
architecture a of n01 is
  function never_called(x : integer) return integer is
  begin
    return no_such_function(x);
  end function;
begin
end architecture;
