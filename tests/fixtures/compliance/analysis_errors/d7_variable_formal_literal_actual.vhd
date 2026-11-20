-- fsim-expect: FSIM-ELAB-VHPROC-018 std=1993
-- SPDX-License-Identifier: Apache-2.0
-- IEEE 1076-2019 4.2.2.3, 6.5.7.1: the actual of a formal of class variable is a variable.
entity d7_variable_formal_literal_actual is
end entity;

architecture a of d7_variable_formal_literal_actual is
begin
  process
    procedure bump(variable v : inout integer) is
    begin
      v := v + 1;
    end procedure;
  begin
    bump(6);
    wait;
  end process;
end architecture;
